#pragma once

#include <utility>

#include "Application/Application.hpp"
#include "AssetManager.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

class ShaderAsset;

class ShaderAsset : public Asset
{
public:
    ShaderAsset(std::filesystem::path path, json metadata)
        : Asset(std::move(path), std::move(metadata))
    {
        auto loaded = Reload();
        if (!loaded)
            throw std::runtime_error(ErrorMessage(loaded.error()));
    }

    ARCADE_ENGINE_API ~ShaderAsset() override = default;
    Result<void> Reload()
    {
        LOG_INFO("Reloading ShaderAsset {}", m_Path.filename().string());

        std::ifstream input(m_Path, std::ios::binary);
        if (!input)
        {
            return MAKE_ERROR_MSG(Error::NotFound, "Could not open shader: " + m_Path.string());
        }
        std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        if (input.bad())
        {
            return MAKE_ERROR_MSG(Error::IoFailure, "Could not read shader: " + m_Path.string());
        }
        m_Source = std::move(source);
        return {};
    }

    void Load() override
    {
        auto loaded = Reload();
        if (!loaded)
            throw std::runtime_error(ErrorMessage(loaded.error()));
    }
    [[nodiscard]] const std::string& Source() const noexcept { return m_Source; }

private:
    std::string m_Source;
};
