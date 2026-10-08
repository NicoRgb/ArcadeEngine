#include "Assets/AssetManager.hpp"

#include <cerrno>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

Result<void> Asset::SetMetadataField(std::string_view key, json value)
{
    if (key.empty())
    {
        return MAKE_ERROR_MSG(Error::InvalidArgument, "Asset metadata keys cannot be empty.");
    }
    if (!m_Metadata.is_object())
    {
        return MAKE_ERROR_MSG(Error::InvalidState, "Asset metadata must be a JSON object.");
    }

    json updatedMetadata = m_Metadata;
    updatedMetadata[std::string(key)] = std::move(value);
    auto written = WriteMetadata(updatedMetadata);
    if (!written)
    {
        return FORWARD_ERROR(written);
    }

    m_Metadata = std::move(updatedMetadata);
    return {};
}

Result<void> Asset::WriteMetadata(const json& metadata) const
{
    auto metadataPath = m_Path;
    metadataPath += ".asset";
    auto temporaryPath = metadataPath;
    temporaryPath += ".tmp";

    std::string serialized;
    try
    {
        serialized = metadata.dump(4);
    }
    catch (const json::exception& exception)
    {
        return MAKE_ERROR_MSG(Error::ParseFailure, "Unable to serialize asset metadata: " +
                                                       std::string(exception.what()));
    }

    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return MAKE_ERROR_EXT(Error::PermissionDenied,
                                  "Unable to create asset metadata file: " + metadataPath.string(),
                                  std::error_code(errno, std::generic_category()));
        }

        output << serialized;
        output.flush();
        if (!output)
        {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporaryPath, ignored);
            return MAKE_ERROR_MSG(Error::IoFailure,
                                  "Unable to write asset metadata file: " + metadataPath.string());
        }
        output.close();
        if (!output)
        {
            std::error_code ignored;
            std::filesystem::remove(temporaryPath, ignored);
            return MAKE_ERROR_MSG(Error::IoFailure,
                                  "Unable to finish asset metadata file: " + metadataPath.string());
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(temporaryPath.c_str(), metadataPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto error =
            std::error_code(static_cast<int>(GetLastError()), std::system_category());
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return MAKE_ERROR_EXT(Error::IoFailure,
                              "Unable to replace asset metadata file: " + metadataPath.string(),
                              error);
    }
#else
    std::error_code error;
    std::filesystem::rename(temporaryPath, metadataPath, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return MAKE_ERROR_EXT(Error::IoFailure,
                              "Unable to replace asset metadata file: " + metadataPath.string(),
                              error);
    }
#endif
    return {};
}
