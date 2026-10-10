#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "AssetManager.hpp"

enum class ShaderStage : uint8_t
{
    Vertex,
    Hull,
    Domain,
    Geometry,
    Fragment,
    Compute,
    RayGeneration,
    Intersection,
    AnyHit,
    ClosestHit,
    Miss,
    Callable,
    Mesh,
    Amplification,
    Unknown,
};

struct ShaderParameter
{
    std::string Name;
    std::string TypeName;
    uint32_t BindingIndex = std::numeric_limits<uint32_t>::max();
    uint32_t BindingSpace = std::numeric_limits<uint32_t>::max();
};

struct ShaderEntryPoint
{
    std::string Name;
    ShaderStage Stage = ShaderStage::Unknown;
    std::vector<std::byte> Code;
    std::vector<ShaderParameter> Parameters;
};

class ShaderAsset : public Asset
{
public:
    ShaderAsset(std::filesystem::path path, json metadata)
        : Asset(std::move(path), std::move(metadata))
    {
    }

    ARCADE_ENGINE_API ~ShaderAsset() override;

    Result<void> Load() override;

    [[nodiscard]] const std::string& Source() const noexcept { return m_Source; }
    [[nodiscard]] const std::string& GetSource() const noexcept { return Source(); }
    [[nodiscard]] const std::vector<ShaderEntryPoint>& GetEntryPoints() const noexcept;
    [[nodiscard]] const std::vector<ShaderParameter>& GetGlobalParameters() const noexcept;

private:
    struct CompiledData
    {
        struct Dependency
        {
            std::filesystem::path Path;
            std::filesystem::file_time_type ModifiedTime;
        };

        std::vector<ShaderEntryPoint> EntryPoints;
        std::vector<ShaderParameter> GlobalParameters;
        std::vector<Dependency> Dependencies;

        bool IsFresh(const std::string& source) const
        {
            if (Dependencies.empty())
            {
                return false;
            }
            for (const Dependency& dependency : Dependencies)
            {
                std::error_code error;
                const auto modifiedTime = std::filesystem::last_write_time(dependency.Path, error);
                if (error || modifiedTime != dependency.ModifiedTime)
                {
                    return false;
                }
            }
            return source == Source;
        }

        std::string Source;
    };

    std::string m_Source;
    std::unique_ptr<CompiledData> m_CompiledData;
};
