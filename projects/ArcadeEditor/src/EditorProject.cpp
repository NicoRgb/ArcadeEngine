#include "ArcadeEditor/EditorProject.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
using json = nlohmann::json;
constexpr int ProjectFormatVersion = 1;

Result<void> WriteJson(const std::filesystem::path& path, const json& value)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to create settings directory.", error);
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return MakeError(Error::PermissionDenied, "Unable to write " + path.string());
        }
        output << value.dump(2) << '\n';
        output.flush();
        if (!output)
        {
            std::filesystem::remove(temporary, error);
            return MakeError(Error::IoFailure, "Unable to write " + path.string());
        }
    }
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto systemError =
            std::error_code(static_cast<int>(GetLastError()), std::system_category());
        std::filesystem::remove(temporary, error);
        return MakeError(Error::IoFailure, "Unable to replace " + path.string(), systemError);
    }
#else
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        const auto replaceError = error;
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return MakeError(Error::IoFailure, "Unable to replace " + path.string(), replaceError);
    }
#endif
    return {};
}

Result<json> ReadJson(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return MakeError(Error::NotFound, "Unable to open " + path.string());
    }
    try
    {
        return json::parse(input);
    }
    catch (const json::exception& exception)
    {
        return MakeError(Error::ParseFailure, path.string() + ": " + exception.what());
    }
}
} // namespace

EditorProjectService::EditorProjectService(std::filesystem::path recentProjectsFile)
    : m_RecentProjectsFile(std::move(recentProjectsFile))
{
}

Result<EditorProject> EditorProject::Create(const std::filesystem::path& root, std::string name)
{
    if (root.empty() || name.empty())
    {
        return MakeError(Error::InvalidArgument, "Project path and name are required.");
    }
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to create project directory.", error);
    }
    const auto descriptor = root / "ArcadeProject.json";
    if (std::filesystem::exists(descriptor, error))
    {
        return MakeError(Error::AlreadyExists, "A project already exists in this directory.");
    }
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to inspect project directory.", error);
    }
    const auto assets = root / "Assets";
    std::filesystem::create_directories(assets, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to create project Assets directory.", error);
    }

    auto normalizedRoot = std::filesystem::weakly_canonical(root, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to resolve project directory.", error);
    }
    json manifest{{"version", ProjectFormatVersion},
                  {"name", name},
                  {"assetRoot", "Assets"},
                  {"settings", json::object()}};
    auto written = WriteJson(normalizedRoot / "ArcadeProject.json", manifest);
    if (!written)
    {
        return MakeError(written.error());
    }
    return EditorProject{std::move(name), normalizedRoot, normalizedRoot / "Assets",
                         json::object()};
}

Result<EditorProject> EditorProject::Open(const std::filesystem::path& descriptor)
{
    std::error_code error;
    const auto normalizedDescriptor = std::filesystem::weakly_canonical(descriptor, error);
    if (error || !std::filesystem::is_regular_file(normalizedDescriptor, error) || error)
    {
        return MakeError(Error::NotFound,
                         "Project descriptor does not exist: " + descriptor.string(), error);
    }
    auto manifestResult = ReadJson(normalizedDescriptor);
    if (!manifestResult)
    {
        return MakeError(manifestResult.error());
    }
    const auto& manifest = *manifestResult;
    if (!manifest.is_object() || !manifest.contains("version") ||
        !manifest["version"].is_number_integer() ||
        manifest["version"].get<int>() != ProjectFormatVersion || !manifest.contains("name") ||
        !manifest["name"].is_string() || !manifest.contains("assetRoot") ||
        !manifest["assetRoot"].is_string())
    {
        return MakeError(Error::Unsupported,
                         "Project manifest has an unsupported or incomplete format.");
    }
    const auto root = normalizedDescriptor.parent_path();
    auto assetRoot =
        std::filesystem::weakly_canonical(root / manifest["assetRoot"].get<std::string>(), error);
    if (error || !std::filesystem::is_directory(assetRoot, error) || error)
    {
        return MakeError(Error::NotFound, "Project asset directory does not exist.", error);
    }
    auto relative = assetRoot.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..")
    {
        return MakeError(Error::InvalidArgument,
                         "Project asset directory must be inside the project.");
    }
    json settings = json::object();
    if (manifest.contains("settings"))
    {
        if (!manifest["settings"].is_object())
        {
            return MakeError(Error::ParseFailure, "Project settings must be a JSON object.");
        }
        settings = manifest["settings"];
    }
    return EditorProject{manifest["name"].get<std::string>(), root, assetRoot, std::move(settings)};
}

Result<EditorProject> EditorProjectService::Create(const std::filesystem::path& root,
                                                   std::string name)
{
    auto project = EditorProject::Create(root, std::move(name));
    if (!project)
    {
        return MakeError(project.error());
    }
    auto remembered = Remember(project->DescriptorPath());
    if (!remembered)
    {
        return MakeError(remembered.error());
    }
    return project;
}

Result<EditorProject> EditorProjectService::Open(const std::filesystem::path& descriptor)
{
    auto project = EditorProject::Open(descriptor);
    if (!project)
    {
        return MakeError(project.error());
    }
    auto remembered = Remember(project->DescriptorPath());
    if (!remembered)
    {
        return MakeError(remembered.error());
    }
    return project;
}

Result<std::vector<std::filesystem::path>> EditorProjectService::RecentProjects() const
{
    std::error_code error;
    if (!std::filesystem::exists(m_RecentProjectsFile, error))
    {
        if (error)
        {
            return MakeError(Error::IoFailure, "Unable to inspect recent-project settings.", error);
        }
        return std::vector<std::filesystem::path>{};
    }
    auto dataResult = ReadJson(m_RecentProjectsFile);
    if (!dataResult)
    {
        return MakeError(dataResult.error());
    }
    if (!dataResult->is_array())
    {
        return MakeError(Error::ParseFailure, "Recent projects setting must be a JSON array.");
    }
    std::vector<std::filesystem::path> result;
    for (const auto& entry : *dataResult)
    {
        if (!entry.is_string())
        {
            continue;
        }
        const auto path = std::filesystem::path(entry.get<std::string>());
        if (std::filesystem::is_regular_file(path, error) && !error)
        {
            result.push_back(path);
        }
        error.clear();
    }
    return result;
}

Result<void> EditorProjectService::Remember(const std::filesystem::path& descriptor)
{
    auto recentResult = RecentProjects();
    if (!recentResult && recentResult.error().Code != Error::NotFound)
    {
        return MakeError(recentResult.error());
    }
    std::vector<std::filesystem::path> recent =
        recentResult ? std::move(*recentResult) : std::vector<std::filesystem::path>{};
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(descriptor, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to resolve project descriptor.", error);
    }
    std::erase(recent, normalized);
    recent.insert(recent.begin(), normalized);
    if (recent.size() > 12)
    {
        recent.resize(12);
    }
    json serialized = json::array();
    for (const auto& path : recent)
    {
        serialized.push_back(path.string());
    }
    return WriteJson(m_RecentProjectsFile, serialized);
}

std::filesystem::path ArcadeUserConfigDirectory()
{
#if defined(_WIN32)
    char* appData = nullptr;
    std::size_t appDataSize = 0;
    if (_dupenv_s(&appData, &appDataSize, "APPDATA") == 0 && appData != nullptr)
    {
        const std::filesystem::path configDirectory =
            std::filesystem::path(appData) / "ArcadeEngine";
        std::free(appData);
        return configDirectory;
    }
    std::free(appData);
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr)
    {
        return std::filesystem::path(xdg) / "ArcadeEngine";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr)
    {
        return std::filesystem::path(home) / ".config" / "ArcadeEngine";
    }
#endif
    return std::filesystem::temp_directory_path() / "ArcadeEngine";
}
