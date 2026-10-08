#include "ArcadeEditor/EditorThumbnail.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_resize.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>

Result<Resource<EditorImage>> EditorThumbnailCache::Get(ResourceManager& resources,
                                                        const std::filesystem::path& path)
{
    if (const auto it = m_Images.find(path); it != m_Images.end())
    {
        return it->second;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return MAKE_ERROR_MSG(Error::PermissionDenied,
                              "Unable to read thumbnail source: " + path.string());
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
    if (input.bad() || bytes.empty() || bytes.size() > 64U * 1024U * 1024U ||
        bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return MAKE_ERROR_MSG(Error::IoFailure,
                              "Thumbnail source is empty or could not be read: " + path.string());
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                             &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        std::uint64_t(width) * std::uint64_t(height) > 16777216ULL)
    {
        if (decoded != nullptr)
            stbi_image_free(decoded);
        return MAKE_ERROR_MSG(Error::Unsupported,
                              "Unable to decode image thumbnail: " + path.filename().string());
    }
    auto image = std::make_shared<EditorImage>();
    constexpr int MaximumPreviewDimension = 256;
    const float scale =
        std::min(1.0F, float(MaximumPreviewDimension) / float(std::max(width, height)));
    image->Width = std::max(1, static_cast<int>(float(width) * scale));
    image->Height = std::max(1, static_cast<int>(float(height) * scale));
    image->Rgba.resize(std::size_t(image->Width) * std::size_t(image->Height) * 4U);
    if (image->Width == width && image->Height == height)
    {
        std::copy(decoded, decoded + image->Rgba.size(), image->Rgba.begin());
    }
    else
    {
        if (stbir_resize_uint8(decoded, width, height, 0, image->Rgba.data(), image->Width,
                               image->Height, 0, 4) == 0)
        {
            stbi_image_free(decoded);
            return MAKE_ERROR_MSG(Error::IoFailure,
                                  "Unable to resize image thumbnail: " + path.filename().string());
        }
    }
    stbi_image_free(decoded);
    auto result = resources.InsertShared<EditorImage>("editor-thumbnail:" + path.generic_string(),
                                                      std::move(image));
    if (!result)
    {
        if (result.error().Code == Error::InvalidArgument)
        {
            auto existing =
                resources.FindShared<EditorImage>("editor-thumbnail:" + path.generic_string());
            if (existing)
            {
                m_Images.emplace(path, *existing);
                return *existing;
            }
        }
        return FORWARD_ERROR(result);
    }
    m_Images.emplace(path, *result);
    return *result;
}

void EditorThumbnailCache::Invalidate(ResourceManager& resources, const std::filesystem::path& path)
{
    const auto it = m_Images.find(path);
    if (it != m_Images.end())
    {
        (void)resources.Unload(it->second.Id());
        m_Images.erase(it);
    }
}

void EditorThumbnailCache::Clear(ResourceManager& resources)
{
    for (const auto& [path, image] : m_Images)
    {
        (void)path;
        (void)resources.Unload(image.Id());
    }
    m_Images.clear();
}
