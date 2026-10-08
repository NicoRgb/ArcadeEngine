#include "ArcadeEditor/EditorAssetWatcher.hpp"

#include <wtr/watcher.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_set>

struct EditorAssetWatcher::Impl
{
    mutable std::mutex Mutex;
    std::unordered_set<std::filesystem::path> Changes;
    std::unique_ptr<wtr::watch> Watch;
    std::filesystem::path Root;
    bool Failed = false;
};

EditorAssetWatcher::EditorAssetWatcher() : m_Impl(std::make_unique<Impl>()) {}
EditorAssetWatcher::~EditorAssetWatcher()
{
    Stop();
}

Result<void> EditorAssetWatcher::Start(const std::filesystem::path& root)
{
    Stop();
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
    {
        return MAKE_ERROR_EXT(Error::NotFound,
                              "Asset watcher root is not an accessible directory: " + root.string(),
                              error);
    }
    m_Impl->Root = canonical;
    m_Impl->Failed = false;
    auto watcher =
        std::make_unique<wtr::watch>(canonical,
                                     [impl = m_Impl.get()](const wtr::event& event)
                                     {
                                         std::scoped_lock lock(impl->Mutex);
                                         if (event.path_type == wtr::event::path_type::watcher)
                                         {
                                             if (event.path_name.string().starts_with("e/"))
                                                 impl->Failed = true;
                                             return;
                                         }
                                         if (event.path_type != wtr::event::path_type::other)
                                         {
                                             impl->Changes.insert(event.path_name);
                                             if (event.associated)
                                                 impl->Changes.insert(event.associated->path_name);
                                         }
                                     });
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Watch = std::move(watcher);
    }
    return {};
}

void EditorAssetWatcher::Stop() noexcept
{
    if (!m_Impl)
    {
        return;
    }
    std::unique_ptr<wtr::watch> watcher;
    if (m_Impl)
    {
        std::scoped_lock lock(m_Impl->Mutex);
        watcher = std::move(m_Impl->Watch);
        m_Impl->Changes.clear();
    }
    if (watcher)
        (void)watcher->close();
}

std::vector<std::filesystem::path> EditorAssetWatcher::DrainChanges()
{
    std::scoped_lock lock(m_Impl->Mutex);
    std::vector<std::filesystem::path> result(m_Impl->Changes.begin(), m_Impl->Changes.end());
    m_Impl->Changes.clear();
    std::ranges::sort(result);
    return result;
}

bool EditorAssetWatcher::IsWatching() const noexcept
{
    std::scoped_lock lock(m_Impl->Mutex);
    return bool(m_Impl->Watch) && !m_Impl->Failed;
}
