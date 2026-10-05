#pragma once

#include "Core/Result.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

struct EditorProject
{
    std::string Name;
    std::filesystem::path Root;
    std::filesystem::path AssetRoot;
    nlohmann::json Settings = nlohmann::json::object();

    [[nodiscard]] static Result<EditorProject> Create(const std::filesystem::path& root,
                                                      std::string name);
    [[nodiscard]] static Result<EditorProject> Open(const std::filesystem::path& descriptor);
    [[nodiscard]] std::filesystem::path DescriptorPath() const
    {
        return Root / "ArcadeProject.json";
    }
};

class EditorProjectService
{
public:
    explicit EditorProjectService(std::filesystem::path recentProjectsFile);

    [[nodiscard]] Result<EditorProject> Create(const std::filesystem::path& root, std::string name);
    [[nodiscard]] Result<EditorProject> Open(const std::filesystem::path& descriptor);
    [[nodiscard]] Result<std::vector<std::filesystem::path>> RecentProjects() const;
    [[nodiscard]] Result<void> Remember(const std::filesystem::path& descriptor);

private:
    std::filesystem::path m_RecentProjectsFile;
};

[[nodiscard]] std::filesystem::path ArcadeUserConfigDirectory();
