#pragma once

#include "Result.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

enum class ResourceType : uint8_t
{
    Shared,
    Owned,
};

struct ResourceId
{
    uint64_t Value = 0;

    friend constexpr bool operator==(ResourceId, ResourceId) = default;
    explicit constexpr operator bool() const noexcept { return Value != 0; }
};

struct ResourceIdHash
{
    size_t operator()(ResourceId rid) const noexcept { return std::hash<uint64_t>{}(rid.Value); }
};

struct ResourceKey
{
    std::type_index Type{typeid(void)};
    std::string Name;

    friend bool operator==(const ResourceKey&, const ResourceKey&) = default;
};

#define GOLDEN_RATIO 0x9e3779b97f4a7c15ULL

struct ResourceKeyHash
{
    size_t operator()(const ResourceKey& key) const noexcept
    {
        const size_t typeHash = key.Type.hash_code();
        const size_t nameHash = std::hash<std::string_view>{}(key.Name);

        return typeHash ^ (nameHash + GOLDEN_RATIO + (typeHash << 6) + (typeHash >> 2));
    }
};

template <typename T>
class Resource
{
public:
    Resource() = default;

    template <typename U>
    Resource(Resource<U> other) noexcept
        requires(std::is_convertible_v<U*, T*>)
        : m_Id(other.m_Id), m_Pointer(other.m_Pointer)
    {
    }

    T* operator->() noexcept { return m_Pointer.get(); }
    const T* operator->() const noexcept { return m_Pointer.get(); }
    T& operator*() noexcept { return *m_Pointer; }
    const T& operator*() const noexcept { return *m_Pointer; }

    template <typename U>
    [[nodiscard]]
    Resource<U> DynamicCast() const noexcept
    {
        auto pointer = std::dynamic_pointer_cast<U>(m_Pointer);
        if (!pointer)
        {
            return {};
        }

        return Resource<U>(m_Id, std::move(pointer));
    }

    template <typename U>
    [[nodiscard]]
    Resource<U> StaticCast() const noexcept
    {
        auto pointer = std::static_pointer_cast<U>(m_Pointer);
        return Resource<U>(m_Id, std::move(pointer));
    }

    [[nodiscard]]
    T* Get() noexcept
    {
        return m_Pointer.get();
    }

    [[nodiscard]]
    const T* Get() const noexcept
    {
        return m_Pointer.get();
    }

    [[nodiscard]]
    ResourceId Id() const noexcept
    {
        return m_Id;
    }

    [[nodiscard]]
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_Pointer);
    }

    friend bool operator==(const Resource&, const Resource&) noexcept = default;

private:
    Resource(ResourceId rid, std::shared_ptr<T> pointer) noexcept
        : m_Id(rid), m_Pointer(std::move(pointer))
    {
    }

    ResourceId m_Id{};
    std::shared_ptr<T> m_Pointer;

    friend class ResourceManager;

    template <typename>
    friend class Resource;

    template <typename>
    friend class WeakResource;

    template <typename>
    friend class BorrowedResource;
};

template <typename T>
class WeakResource
{
public:
    WeakResource() = default;

    WeakResource(const Resource<T>& resource) noexcept
        : m_Id(resource.m_Id), m_Pointer(resource.m_Pointer)
    {
    }

    [[nodiscard]]
    bool Expired() const noexcept
    {
        return m_Pointer.expired();
    }

    [[nodiscard]]
    explicit operator bool() const noexcept
    {
        return !Expired();
    }

    [[nodiscard]]
    ResourceId Id() const noexcept
    {
        return m_Id;
    }

    [[nodiscard]]
    std::optional<Resource<T>> Lock() const noexcept
    {
        auto pointer = m_Pointer.lock();

        if (!pointer)
        {
            return std::nullopt;
        }

        return Resource<T>(m_Id, std::move(pointer));
    }

private:
    ResourceId m_Id{};
    std::weak_ptr<T> m_Pointer;
};

template <typename T>
class BorrowedResource
{
public:
    BorrowedResource() = default;

    [[nodiscard]]
    ResourceId Id() const noexcept
    {
        return m_Id;
    }

    [[nodiscard]]
    bool Expired() const noexcept
    {
        return m_Pointer.expired();
    }

    [[nodiscard]]
    explicit operator bool() const noexcept
    {
        return !Expired();
    }

    [[nodiscard]]
    std::optional<Resource<T>> Lock() const noexcept
    {
        auto owner = m_Pointer.lock();
        if (!owner)
        {
            return std::nullopt;
        }

        return Resource<T>(m_Id, std::move(owner));
    }

private:
    BorrowedResource(ResourceId rid, const std::shared_ptr<T>& pointer) noexcept
        : m_Id(rid), m_Pointer(pointer)
    {
    }

    ResourceId m_Id{};
    std::weak_ptr<T> m_Pointer;

    friend class ResourceManager;
};

class ResourceManager
{
private:
    struct EntryBase
    {
        virtual ~EntryBase() = default;

        ResourceId Id{};
        ResourceType Type = ResourceType::Shared;
        ResourceKey Key{};
    };

    template <typename T>
    struct Entry final : EntryBase
    {
        std::shared_ptr<T> Object;
    };

public:
    ResourceManager() = default;

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    ~ResourceManager() = default;

    template <typename T, typename... Args>
    [[nodiscard]]
    Result<Resource<T>> CreateShared(std::string_view name, Args&&... args)
    {
        auto object = std::make_shared<T>(std::forward<Args>(args)...);
        return InsertShared<T>(name, std::move(object));
    }

    template <typename T>
    [[nodiscard]]
    Result<Resource<T>> InsertShared(std::string_view name, std::shared_ptr<T> object)
    {
        return Insert<T>(ResourceType::Shared, name, std::move(object));
    }

    template <typename T, typename... Args>
    [[nodiscard]]
    Result<BorrowedResource<T>> CreateOwned(std::string_view name, Args&&... args)
    {
        auto object = std::make_shared<T>(std::forward<Args>(args)...);

        return InsertOwned<T>(name, std::move(object));
    }

    template <typename T>
    [[nodiscard]]
    Result<BorrowedResource<T>> InsertOwned(std::string_view name, std::shared_ptr<T> object)
    {
        if (!object)
        {
            return MakeError(Error::InvalidArgument);
        }

        auto result = Insert<T>(ResourceType::Owned, name, std::move(object));

        if (!result)
        {
            return MakeError(result.error());
        }

        const auto* entry = static_cast<const Entry<T>*>(m_Entries.at(result->Id()).get());

        return BorrowedResource<T>(result->Id(), entry->Object);
    }

    template <typename T>
    [[nodiscard]]
    Result<BorrowedResource<T>> InsertOwned(std::string_view name, std::unique_ptr<T> object)
    {
        if (!object)
        {
            return MakeError(Error::InvalidArgument);
        }

        return InsertOwned<T>(name, std::shared_ptr<T>(std::move(object)));
    }

    template <typename T>
    [[nodiscard]]
    Result<Resource<T>> GetShared(ResourceId rid) const
    {
        const Entry<T>* entry = GetEntry<T>(rid);

        if (!entry || entry->Type != ResourceType::Shared)
        {
            return MakeError(Error::NotFound);
        }

        return Resource<T>(rid, entry->Object);
    }

    template <typename T>
    [[nodiscard]]
    Result<BorrowedResource<T>> GetOwned(ResourceId rid) const
    {
        const Entry<T>* entry = GetEntry<T>(rid);

        if (!entry || entry->Type != ResourceType::Owned)
        {
            return MakeError(Error::NotFound);
        }

        return BorrowedResource<T>(rid, entry->Object);
    }

    template <typename T>
    [[nodiscard]]
    Result<Resource<T>> FindShared(std::string_view name) const
    {
        const auto rid = FindId<T>(name);

        if (!rid)
        {
            return MakeError(Error::NotFound);
        }

        return GetShared<T>(*rid);
    }

    template <typename T>
    [[nodiscard]]
    Result<BorrowedResource<T>> FindOwned(std::string_view name) const
    {
        const auto rid = FindId<T>(name);

        if (!rid)
        {
            return MakeError(Error::NotFound);
        }

        return GetOwned<T>(*rid);
    }

    template <typename T, typename... Args>
    [[nodiscard]]
    Result<Resource<T>> GetOrCreateShared(std::string_view name, Args&&... args)
    {
        if (auto existing = FindShared<T>(name); existing)
        {
            return *existing;
        }

        return CreateShared<T>(name, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    [[nodiscard]]
    Result<Resource<T>> GetSingleton(Args&&... args)
    {
        constexpr std::string_view singletonName = "@singleton";
        return GetOrCreateShared<T>(singletonName, std::forward<Args>(args)...);
    }

    [[nodiscard]]
    Result<void> Unload(ResourceId rid)
    {
        const auto it = m_Entries.find(rid);

        if (it == m_Entries.end())
        {
            return MakeError(Error::NotFound);
        }

        EraseEntry(it);
        return {};
    }

    [[nodiscard]]
    Result<void> UnloadOwned(ResourceId rid)
    {
        const auto it = m_Entries.find(rid);

        if (it == m_Entries.end() || it->second->Type != ResourceType::Owned)
        {
            return MakeError(Error::NotFound);
        }

        EraseEntry(it);
        return {};
    }

    template <typename T>
    [[nodiscard]]
    Result<void> UnloadOwned(const BorrowedResource<T>& resource)
    {
        return UnloadOwned(resource.Id());
    }

    [[nodiscard]]
    bool Contains(ResourceId id) const noexcept
    {
        return m_Entries.contains(id);
    }

private:
    template <typename T>
    [[nodiscard]]
    Result<Resource<T>> Insert(ResourceType type, std::string_view name, std::shared_ptr<T> object)
    {
        if (!object)
        {
            return MakeError(Error::InvalidArgument);
        }

        const ResourceKey key{.Type = typeid(T), .Name = std::string(name)};

        if (!name.empty() && m_ByKey.contains(key))
        {
            return MakeError(Error::InvalidArgument);
        }

        const ResourceId id = NextId();

        auto entry = std::make_unique<Entry<T>>();
        entry->Id = id;
        entry->Type = type;
        entry->Key = key;
        entry->Object = std::move(object);

        if (!name.empty())
        {
            m_ByKey.emplace(entry->Key, id);
        }

        m_Entries.emplace(id, std::move(entry));

        const auto* stored = static_cast<const Entry<T>*>(m_Entries.at(id).get());
        return Resource<T>(id, stored->Object);
    }

    template <typename T>
    [[nodiscard]]
    std::optional<ResourceId> FindId(std::string_view name) const
    {
        if (name.empty())
        {
            return std::nullopt;
        }

        const ResourceKey key{.Type = typeid(T), .Name = std::string(name)};

        const auto it = m_ByKey.find(key);

        if (it == m_ByKey.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    template <typename T>
    [[nodiscard]]
    const Entry<T>* GetEntry(ResourceId id) const noexcept
    {
        const auto it = m_Entries.find(id);

        if (it == m_Entries.end())
        {
            return nullptr;
        }

        if (it->second->Key.Type != typeid(T))
        {
            return nullptr;
        }

        return static_cast<const Entry<T>*>(it->second.get());
    }

    ResourceId NextId() noexcept
    {
        // 0 is reserved as invalid ID
        ResourceId result{m_NextId++};

        if (!result)
        {
            result = ResourceId{m_NextId++};
        }

        return result;
    }

    void EraseEntry(
        std::unordered_map<ResourceId, std::unique_ptr<EntryBase>, ResourceIdHash>::iterator it)
    {
        if (!it->second->Key.Name.empty())
        {
            m_ByKey.erase(it->second->Key);
        }

        m_Entries.erase(it);
    }

    std::unordered_map<ResourceId, std::unique_ptr<EntryBase>, ResourceIdHash> m_Entries;
    std::unordered_map<ResourceKey, ResourceId, ResourceKeyHash> m_ByKey;

    uint64_t m_NextId = 1;
};
