#pragma once

#include "Core/Resource.hpp"

#include <filesystem>
#include <unordered_map>
#include <vector>

struct EditorImage
{
    int Width = 0;
    int Height = 0;
    std::vector<unsigned char> Rgba;
};

class EditorThumbnailCache
{
public:
    [[nodiscard]] Result<Resource<EditorImage>> Get(ResourceManager& resources,
                                                    const std::filesystem::path& path);
    void Invalidate(ResourceManager& resources, const std::filesystem::path& path);
    void Clear(ResourceManager& resources);

private:
    std::unordered_map<std::filesystem::path, Resource<EditorImage>> m_Images;
};
