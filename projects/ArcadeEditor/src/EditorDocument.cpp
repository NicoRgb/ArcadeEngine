#include "ArcadeEditor/EditorDocument.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
constexpr std::uintmax_t MaxDocumentBytes = 32U * 1024U * 1024U;
constexpr std::uintmax_t MaxRecoveryBytes = 128U * 1024U * 1024U;

Result<std::filesystem::path> NormalizeExistingPath(const std::filesystem::path& path)
{
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return MAKE_ERROR_EXT(Error::IoFailure, "Unable to resolve document path: " + path.string(),
                              error);
    }
    return normalized;
}

std::filesystem::path MakeTemporaryPath(const std::filesystem::path& path)
{
    return path.parent_path() /
           (path.filename().string() + ".arcade-tmp-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

Result<void> ReplaceFile(const std::filesystem::path& temporary,
                         const std::filesystem::path& destination)
{
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto error =
            std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return MAKE_ERROR_EXT(Error::IoFailure,
                              "Unable to atomically replace file: " + destination.string(), error);
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error)
    {
        return MAKE_ERROR_EXT(Error::IoFailure,
                              "Unable to atomically replace file: " + destination.string(), error);
    }
#endif
    return {};
}
} // namespace

EditorDocument::EditorDocument(std::filesystem::path path, std::string text)
    : m_Path(std::move(path)), m_Text(std::move(text)), m_SavedText(m_Text)
{
}

Result<void> EditorDocument::Save()
{
    const auto temporary = MakeTemporaryPath(m_Path);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return MAKE_ERROR_EXT(Error::PermissionDenied,
                                  "Unable to create temporary file for " + m_Path.string(),
                                  std::error_code(errno, std::generic_category()));
        }
        output.write(m_Text.data(), static_cast<std::streamsize>(m_Text.size()));
        output.flush();
        if (!output)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return MAKE_ERROR_MSG(Error::IoFailure,
                                  "Unable to write temporary file for " + m_Path.string());
        }
    }

    auto replaced = ReplaceFile(temporary, m_Path);
    if (!replaced)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return FORWARD_ERROR(replaced);
    }
    m_SavedText = m_Text;
    m_ExternalConflict = false;
    return {};
}

Result<void> EditorDocument::ReloadFromDisk()
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(m_Path, error) || error)
    {
        m_ExternalConflict = true;
        return MAKE_ERROR_EXT(Error::NotFound,
                              "Document was removed or replaced: " + m_Path.string(), error);
    }
    const auto size = std::filesystem::file_size(m_Path, error);
    if (error || size > MaxDocumentBytes)
    {
        m_ExternalConflict = true;
        return MAKE_ERROR_EXT(error ? Error::IoFailure : Error::Unsupported,
                              "Changed document cannot be reloaded: " + m_Path.string(), error);
    }
    std::ifstream input(m_Path, std::ios::binary);
    if (!input)
    {
        m_ExternalConflict = true;
        return MAKE_ERROR_MSG(Error::PermissionDenied,
                              "Unable to reload document: " + m_Path.string());
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad() || text.find('\0') != std::string::npos)
    {
        m_ExternalConflict = true;
        return MAKE_ERROR_MSG(Error::IoFailure,
                              "Changed document is unreadable text: " + m_Path.string());
    }
    m_Text = text;
    m_SavedText = std::move(text);
    m_ExternalConflict = false;
    return {};
}

Result<std::shared_ptr<EditorDocument>> EditorDocumentStore::Open(const std::filesystem::path& path)
{
    auto normalizedResult = NormalizeExistingPath(path);
    if (!normalizedResult)
    {
        return FORWARD_ERROR(normalizedResult);
    }
    const auto normalized = std::move(*normalizedResult);
    for (const auto& document : m_Documents)
    {
        if (document->Path() == normalized)
        {
            return document;
        }
    }
    if (const auto cached = m_DocumentCache.find(normalized); cached != m_DocumentCache.end())
    {
        if (auto document = cached->second.lock())
        {
            m_Documents.push_back(document);
            return document;
        }
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(normalized, error) || error)
    {
        return MAKE_ERROR_EXT(Error::InvalidArgument,
                              "Path is not a readable regular file: " + path.string(), error);
    }
    const auto size = std::filesystem::file_size(normalized, error);
    if (error)
    {
        return MAKE_ERROR_EXT(Error::IoFailure, "Unable to inspect file: " + normalized.string(),
                              error);
    }
    if (size > MaxDocumentBytes)
    {
        return MAKE_ERROR_MSG(Error::Unsupported,
                              "Text documents are limited to 32 MiB: " + normalized.string());
    }

    std::ifstream input(normalized, std::ios::binary);
    if (!input)
    {
        return MAKE_ERROR_EXT(Error::PermissionDenied,
                              "Unable to open file: " + normalized.string(),
                              std::error_code(errno, std::generic_category()));
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad())
    {
        return MAKE_ERROR_MSG(Error::IoFailure, "Unable to read file: " + normalized.string());
    }
    if (text.find('\0') != std::string::npos)
    {
        return MAKE_ERROR_MSG(Error::Unsupported,
                              "Binary assets cannot be opened in a text editor: " +
                                  normalized.string());
    }

    auto document =
        std::shared_ptr<EditorDocument>(new EditorDocument(normalized, std::move(text)));
    m_Documents.push_back(document);
    m_DocumentCache.insert_or_assign(normalized, document);
    return document;
}

Result<void> EditorDocumentStore::Close(const std::shared_ptr<EditorDocument>& document)
{
    if (!document || std::ranges::find(m_Documents, document) == m_Documents.end())
    {
        return MAKE_ERROR_MSG(Error::NotFound, "Document is not open.");
    }
    std::erase(m_Documents, document);
    return {};
}

Result<void> EditorDocumentStore::SaveAll()
{
    for (const auto& document : m_Documents)
    {
        if (document->IsDirty())
        {
            auto result = document->Save();
            if (!result)
            {
                return FORWARD_ERROR(result);
            }
        }
    }
    return {};
}

Result<void> EditorDocumentStore::ChangePath(const std::filesystem::path& from,
                                             const std::filesystem::path& to)
{
    auto oldResult = NormalizeExistingPath(from);
    if (!oldResult)
    {
        return FORWARD_ERROR(oldResult);
    }
    auto newResult = NormalizeExistingPath(to);
    if (!newResult)
    {
        return FORWARD_ERROR(newResult);
    }
    const auto oldPath = std::move(*oldResult);
    const auto newPath = std::move(*newResult);
    std::shared_ptr<EditorDocument> renamedDocument;
    for (const auto& document : m_Documents)
    {
        if (document->Path() == oldPath)
        {
            renamedDocument = document;
            break;
        }
    }
    if (!renamedDocument)
    {
        const auto cached = m_DocumentCache.find(oldPath);
        if (cached != m_DocumentCache.end())
        {
            renamedDocument = cached->second.lock();
        }
    }
    m_DocumentCache.erase(oldPath);
    if (renamedDocument)
    {
        renamedDocument->m_Path = newPath;
        m_DocumentCache.insert_or_assign(newPath, renamedDocument);
    }
    return {};
}

Result<void> EditorDocumentStore::ReloadExternalChange(const std::filesystem::path& path)
{
    auto normalized = NormalizeExistingPath(path);
    if (!normalized)
    {
        return FORWARD_ERROR(normalized);
    }
    const auto document = std::ranges::find_if(m_Documents, [&normalized](const auto& candidate)
                                               { return candidate->Path() == *normalized; });
    if (document == m_Documents.end())
    {
        return {};
    }
    if ((*document)->IsDirty())
    {
        (*document)->m_ExternalConflict = true;
        return MAKE_ERROR_MSG(Error::InvalidState,
                              "File changed on disk; your unsaved edits are preserved.");
    }
    return (*document)->ReloadFromDisk();
}

Result<void> EditorDocumentStore::SaveRecovery(const std::filesystem::path& recoveryFile) const
{
    using json = nlohmann::json;
    json snapshots = json::array();
    std::uintmax_t totalBytes = 0;
    for (const auto& document : m_Documents)
    {
        if (!document->IsDirty())
            continue;
        totalBytes += document->Text().size();
        if (totalBytes > MaxRecoveryBytes)
        {
            return MAKE_ERROR_MSG(Error::Unsupported, "Unsaved document recovery exceeds 128 MiB.");
        }
        snapshots.push_back({{"path", document->Path().string()}, {"text", document->Text()}});
    }
    std::error_code error;
    std::filesystem::create_directories(recoveryFile.parent_path(), error);
    if (error)
    {
        return MAKE_ERROR_EXT(Error::IoFailure, "Unable to create recovery directory.", error);
    }
    auto temporary = recoveryFile;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return MAKE_ERROR_MSG(Error::PermissionDenied, "Unable to create recovery file.");
        output << snapshots.dump();
        output.flush();
        if (!output)
        {
            std::filesystem::remove(temporary, error);
            return MAKE_ERROR_MSG(Error::IoFailure, "Unable to write recovery file.");
        }
    }
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), recoveryFile.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto systemError =
            std::error_code(static_cast<int>(GetLastError()), std::system_category());
        std::filesystem::remove(temporary, error);
        return MAKE_ERROR_EXT(Error::IoFailure, "Unable to replace recovery file.", systemError);
    }
#else
    std::filesystem::rename(temporary, recoveryFile, error);
    if (error)
    {
        const auto replaceError = error;
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return MAKE_ERROR_EXT(Error::IoFailure, "Unable to replace recovery file.", replaceError);
    }
#endif
    return {};
}

Result<std::size_t> EditorDocumentStore::RestoreRecovery(const std::filesystem::path& recoveryFile)
{
    using json = nlohmann::json;
    std::error_code error;
    if (!std::filesystem::exists(recoveryFile, error))
    {
        return error ? Result<std::size_t>(MAKE_ERROR_EXT(
                           Error::IoFailure, "Unable to inspect recovery file.", error))
                     : Result<std::size_t>(std::size_t{0});
    }
    const auto size = std::filesystem::file_size(recoveryFile, error);
    if (error || size > MaxRecoveryBytes)
    {
        return MAKE_ERROR_EXT(error ? Error::IoFailure : Error::Unsupported,
                              "Recovery file is unreadable or too large.", error);
    }
    std::ifstream input(recoveryFile, std::ios::binary);
    if (!input)
        return MAKE_ERROR_MSG(Error::PermissionDenied, "Unable to read recovery file.");
    json snapshots;
    try
    {
        snapshots = json::parse(input);
    }
    catch (const json::exception& exception)
    {
        return MAKE_ERROR_MSG(Error::ParseFailure,
                              "Recovery file is invalid: " + std::string(exception.what()));
    }
    if (!snapshots.is_array())
        return MAKE_ERROR_MSG(Error::ParseFailure, "Recovery data must be an array.");
    std::size_t restored = 0;
    for (const auto& snapshot : snapshots)
    {
        if (!snapshot.is_object() || !snapshot.contains("path") || !snapshot["path"].is_string() ||
            !snapshot.contains("text") || !snapshot["text"].is_string())
        {
            return MAKE_ERROR_MSG(Error::ParseFailure, "Recovery entry is incomplete.");
        }
        const auto path = std::filesystem::path(snapshot["path"].get<std::string>());
        const auto& text = snapshot["text"].get_ref<const std::string&>();
        if (text.size() > MaxDocumentBytes || text.find('\0') != std::string::npos)
        {
            return MAKE_ERROR_MSG(Error::Unsupported,
                                  "Recovery entry is too large or contains binary data.");
        }
        auto document = Open(path);
        if (!document)
            return FORWARD_ERROR(document);
        (*document)->SetText(text);
        ++restored;
    }
    return restored;
}
