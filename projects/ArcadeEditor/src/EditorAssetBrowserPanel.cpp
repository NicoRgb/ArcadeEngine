#include "ArcadeEditor/EditorAssetWatcher.hpp"
#include "ArcadeEditor/EditorPanel.hpp"
#include "ArcadeEditor/EditorTheme.hpp"
#include "ArcadeEditor/EditorThumbnail.hpp"

#include "Assets/AssetManager.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "EditorPanelFactories.hpp"

namespace
{
ImVec4 Color(unsigned int rgb, float alpha = 1.0F)
{
    return ImVec4(static_cast<float>((rgb >> 16U) & 0xffU) / 255.0F,
                  static_cast<float>((rgb >> 8U) & 0xffU) / 255.0F,
                  static_cast<float>(rgb & 0xffU) / 255.0F, alpha);
}

std::string Lower(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    const auto relative = candidate.lexically_relative(root);
    return !relative.empty() && *relative.begin() != ".." && relative != ".";
}

unsigned int FileColor(const std::filesystem::path& path)
{
    const std::string extension = Lower(path.extension().string());
    if (extension == ".json")
    {
        return 0x4fc7ac;
    }
    if (extension == ".hlsl" || extension == ".glsl" || extension == ".slang" ||
        extension == ".vert" || extension == ".frag" || extension == ".comp")
    {
        return 0x9f82ff;
    }
    if (extension == ".cpp" || extension == ".hpp" || extension == ".h" || extension == ".c")
    {
        return 0xf0b65c;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga")
    {
        return 0xe77bc2;
    }
    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg")
    {
        return 0x58a9e8;
    }
    return 0x8e9bac;
}

void DrawFileIconAt(const std::filesystem::path& path, bool directory, ImVec2 origin,
                    ImVec2 size = ImVec2(28, 28))
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = 5.0F;
    const unsigned int accent = directory ? 0xe6b85c : FileColor(path);
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                        IM_COL32(31, 38, 50, 255), rounding);
    if (directory)
    {
        const float x = origin.x + size.x * 0.2F;
        const float y = origin.y + size.y * 0.32F;
        draw->AddRectFilled(
            ImVec2(x, y), ImVec2(origin.x + size.x * 0.8F, origin.y + size.y * 0.75F),
            IM_COL32((accent >> 16U) & 0xffU, (accent >> 8U) & 0xffU, accent & 0xffU, 255), 3.0F);
        draw->AddRectFilled(ImVec2(x, y - 3.0F), ImVec2(x + size.x * 0.27F, y + 2.0F),
                            IM_COL32(245, 203, 112, 255), 2.0F);
    }
    else
    {
        const ImVec2 a(origin.x + size.x * 0.25F, origin.y + size.y * 0.14F);
        const ImVec2 b(origin.x + size.x * 0.63F, origin.y + size.y * 0.14F);
        const ImVec2 c(origin.x + size.x * 0.78F, origin.y + size.y * 0.29F);
        const ImVec2 d(origin.x + size.x * 0.78F, origin.y + size.y * 0.84F);
        const ImVec2 e(origin.x + size.x * 0.25F, origin.y + size.y * 0.84F);
        draw->AddConvexPolyFilled(std::array<ImVec2, 5>{a, b, c, d, e}.data(), 5,
                                  IM_COL32(216, 224, 237, 255));
        draw->AddLine(
            ImVec2(origin.x + size.x * 0.35F, origin.y + size.y * 0.48F),
            ImVec2(origin.x + size.x * 0.67F, origin.y + size.y * 0.48F),
            IM_COL32((accent >> 16U) & 0xffU, (accent >> 8U) & 0xffU, accent & 0xffU, 255), 2.0F);
        draw->AddLine(
            ImVec2(origin.x + size.x * 0.35F, origin.y + size.y * 0.63F),
            ImVec2(origin.x + size.x * 0.64F, origin.y + size.y * 0.63F),
            IM_COL32((accent >> 16U) & 0xffU, (accent >> 8U) & 0xffU, accent & 0xffU, 255), 2.0F);
    }
}

class AssetBrowserPanel final : public EditorPanel
{
public:
    std::string_view Id() const noexcept override { return "asset-browser"; }
    std::string_view Title() const noexcept override { return "Asset Browser"; }

    void Draw(EditorPanelContext& context) override
    {
        std::error_code rootError;
        const auto requestedRoot = std::filesystem::weakly_canonical(context.AssetRoot, rootError);
        if (rootError)
        {
            ImGui::TextColored(Color(0xff766f), "Project asset folder is unavailable: %s",
                               rootError.message().c_str());
            return;
        }
        if (m_Root != requestedRoot)
        {
            if (context.Resources != nullptr)
                m_ThumbnailCache.Clear(*context.Resources);
            for (const auto& [path, texture] : m_GpuTextures)
            {
                (void)path;
                glDeleteTextures(1, &texture);
            }
            m_GpuTextures.clear();
            m_ModifiedTimes.clear();
            m_FolderCache.clear();
            m_Root = requestedRoot;
            m_Current = m_Root;
            m_LastScan = {};
            auto watching = m_Watcher.Start(m_Root);
            if (!watching)
            {
                context.Status = ErrorMessage(watching.error());
                if (context.Logs)
                    context.Logs->ReceiveMessage(LogLevel::Warn, context.Status);
            }
        }

        const auto changes = m_Watcher.DrainChanges();
        if (!changes.empty())
        {
            m_LastScan = {};
            bool needsAssetRescan = false;
            for (const auto& changedPath : changes)
            {
                std::error_code pathError;
                const auto normalized = std::filesystem::weakly_canonical(changedPath, pathError);
                if (pathError || !IsWithin(m_Root, normalized))
                    continue;
                needsAssetRescan = true;
                if (context.Resources)
                    m_ThumbnailCache.Invalidate(*context.Resources, normalized);
                if (const auto texture = m_GpuTextures.find(normalized);
                    texture != m_GpuTextures.end())
                {
                    glDeleteTextures(1, &texture->second);
                    m_GpuTextures.erase(texture);
                }
                auto reloaded = context.Documents.ReloadExternalChange(normalized);
                if (!reloaded)
                {
                    context.Status = ErrorMessage(reloaded.error());
                    if (context.Logs)
                        context.Logs->ReceiveMessage(LogLevel::Warn, context.Status);
                }
            }
            if (needsAssetRescan && context.Assets != nullptr)
            {
                auto indexed = context.Assets->RescanAssets(m_Root);
                if (!indexed)
                {
                    context.Status = "Asset rescan failed: " + ErrorMessage(indexed.error());
                    if (context.Logs)
                        context.Logs->ReceiveMessage(LogLevel::Error, context.Status);
                }
            }
        }

        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputTextWithHint("##asset-filter", "Search assets...", m_Filter.data(),
                                 m_Filter.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh"))
            m_LastScan = {};
        ImGui::Separator();

        if (ImGui::BeginTable("AssetBrowserLayout", 2,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, -ImGui::GetFrameHeightWithSpacing())))
        {
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 205.0F);
            ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::BeginChild("AssetFolderTree", ImVec2(0, 0)))
            {
                if (ImGui::ArrowButton("##expand-folders", ImGuiDir_Down))
                    SetFolderExpansion(m_Root, true);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Expand all folders");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##collapse-folders", ImGuiDir_Up))
                    SetFolderExpansion(m_Root, false);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Collapse all folders");
                ImGui::SameLine();
                ImGui::TextDisabled("Folders");
                ImGui::Separator();
                DrawFolderTree(context, m_Root);
            }
            ImGui::EndChild();
            ImGui::TableSetColumnIndex(1);

            if (ImGui::BeginChild("AssetContents", ImVec2(0, 0)))
            {
                DrawBreadcrumbs();
                ImGui::Separator();

                const auto now = std::chrono::steady_clock::now();
                if (m_LastScan.time_since_epoch().count() == 0 ||
                    now - m_LastScan > std::chrono::milliseconds(650))
                {
                    RefreshEntries(context);
                    m_FolderCache.clear();
                    m_LastScan = now;
                }

                if (!m_ScanError.empty())
                {
                    ImGui::TextColored(Color(0xff766f), "Can't read this folder: %s",
                                       m_ScanError.c_str());
                }
                else if (ImGui::BeginChild("AssetRows", ImVec2(0, 0)))
                {
                    DrawEntries(context, m_Entries);
                }
                ImGui::EndChild();
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
        else
        {
            return;
        }

        if (m_OpenRenamePopup)
        {
            ImGui::OpenPopup("Rename Asset");
            m_OpenRenamePopup = false;
        }
        if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Rename %s", m_RenamePath.filename().string().c_str());
            ImGui::SetNextItemWidth(320.0F);
            const bool submit =
                ImGui::InputText("New name", m_RenameBuffer.data(), m_RenameBuffer.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
            const bool renameClicked = ImGui::Button("Rename") || submit;
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            if (renameClicked)
            {
                const std::filesystem::path filename(m_RenameBuffer.data());
                if (filename.empty() || filename.filename() != filename || filename == "." ||
                    filename == "..")
                {
                    context.Status = "Enter a file name without a directory path";
                }
                else
                {
                    const auto oldPath = m_RenamePath;
                    const auto newPath = oldPath.parent_path() / filename;
                    auto rename = context.History.Execute(std::make_unique<RenameFileCommand>(
                        oldPath, newPath, [&context](const auto& from, const auto& to)
                        { return context.Documents.ChangePath(from, to); }));
                    if (rename)
                    {
                        context.SelectedAsset = newPath;
                        context.Status = "Renamed file to " + filename.string();
                        m_LastScan = {};
                        ImGui::CloseCurrentPopup();
                    }
                    else
                        context.Status = ErrorMessage(rename.error());
                }
            }
            ImGui::EndPopup();
        }
        ImGui::TextDisabled("%zu items  ·  Double click to open", m_Entries.size());
    }

private:
    void DrawAssetContextMenu(const std::filesystem::path& path, bool directory,
                              const std::string& name, EditorPanelContext& context)
    {
        if (!ImGui::BeginPopupContextItem("AssetContext"))
            return;
        if (ImGui::MenuItem(directory ? "Open folder" : "Open file"))
        {
            if (directory)
                NavigateTo(path, context);
            else
            {
                auto opened = context.OpenFile(path);
                context.Status = opened ? "Opened " + name : ErrorMessage(opened.error());
            }
        }
        if (ImGui::MenuItem("Copy path"))
        {
            ImGui::SetClipboardText(path.string().c_str());
            context.Status = "Copied asset path";
        }
        if (!directory && ImGui::MenuItem("Rename..."))
        {
            m_RenamePath = path;
            m_RenameBuffer.fill('\0');
            const std::string filename = path.filename().string();
            std::snprintf(m_RenameBuffer.data(), m_RenameBuffer.size(), "%s", filename.c_str());
            m_OpenRenamePopup = true;
        }
        ImGui::EndPopup();
    }

    void RefreshEntries(EditorPanelContext& context)
    {
        std::error_code error;
        std::vector<std::filesystem::directory_entry> entries;
        std::filesystem::directory_iterator iterator(m_Current, error);
        for (const std::filesystem::directory_iterator end; !error && iterator != end;
             iterator.increment(error))
        {
            entries.push_back(*iterator);
        }
        if (error)
        {
            m_ScanError = error.message();
            return;
        }
        m_ScanError.clear();
        std::unordered_map<std::filesystem::path, std::filesystem::file_time_type> currentTimes;
        for (const auto& entry : entries)
        {
            if (!entry.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }
            const auto path = entry.path();
            const auto modified = entry.last_write_time(error);
            if (error)
            {
                error.clear();
                continue;
            }
            currentTimes.emplace(path, modified);
            const auto old = m_ModifiedTimes.find(path);
            if (old != m_ModifiedTimes.end() && old->second != modified)
            {
                if (context.Resources != nullptr)
                    m_ThumbnailCache.Invalidate(*context.Resources, path);
                if (const auto texture = m_GpuTextures.find(path); texture != m_GpuTextures.end())
                {
                    glDeleteTextures(1, &texture->second);
                    m_GpuTextures.erase(texture);
                }
            }
        }
        for (const auto& [oldPath, timestamp] : m_ModifiedTimes)
        {
            (void)timestamp;
            if (currentTimes.contains(oldPath))
                continue;
            if (context.Resources != nullptr)
                m_ThumbnailCache.Invalidate(*context.Resources, oldPath);
            if (const auto texture = m_GpuTextures.find(oldPath); texture != m_GpuTextures.end())
            {
                glDeleteTextures(1, &texture->second);
                m_GpuTextures.erase(texture);
            }
        }
        m_ModifiedTimes = std::move(currentTimes);
        std::ranges::sort(entries,
                          [](const auto& lhs, const auto& rhs)
                          {
                              std::error_code leftError, rightError;
                              const bool leftDirectory = lhs.is_directory(leftError);
                              const bool rightDirectory = rhs.is_directory(rightError);
                              if (leftDirectory != rightDirectory)
                                  return leftDirectory;
                              return Lower(lhs.path().filename().string()) <
                                     Lower(rhs.path().filename().string());
                          });
        m_Entries = std::move(entries);
    }

    void DrawFolderTree(EditorPanelContext& context, const std::filesystem::path& path)
    {
        if (!m_FolderTraversal.insert(path).second)
            return;
        const bool selected = m_Current == path;
        const auto title = path == m_Root ? "Assets" : path.filename().string();
        const auto id = path.generic_string();
        ImGui::PushID(id.c_str());
        std::error_code error;
        std::vector<std::filesystem::path> folders;
        if (const auto cached = m_FolderCache.find(path); cached != m_FolderCache.end())
        {
            folders = cached->second;
        }
        else if (std::filesystem::is_directory(path, error))
        {
            for (std::filesystem::directory_iterator it(path, error), end; !error && it != end;
                 it.increment(error))
            {
                if (it->is_directory(error) && !error)
                {
                    const auto candidate = std::filesystem::weakly_canonical(it->path(), error);
                    if (!error && IsWithin(m_Root, candidate))
                        folders.push_back(candidate);
                }
                error.clear();
            }
            m_FolderCache.emplace(path, folders);
        }
        const auto flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                           (folders.empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                           (selected ? ImGuiTreeNodeFlags_Selected : 0) |
                           (path == m_Root ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        const bool open = ImGui::TreeNodeEx(title.c_str(), flags);
        if (ImGui::IsItemClicked())
            m_Current = path;
        if (open)
        {
            std::ranges::sort(
                folders, [](const auto& a, const auto& b)
                { return Lower(a.filename().string()) < Lower(b.filename().string()); });
            for (const auto& folder : folders)
                DrawFolderTree(context, folder);
            ImGui::TreePop();
        }
        ImGui::PopID();
        m_FolderTraversal.erase(path);
    }

    void SetFolderExpansion(const std::filesystem::path& path, bool expanded)
    {
        if (!m_FolderTraversal.insert(path).second)
            return;
        const auto id = path.generic_string();
        const auto title = path == m_Root ? "Assets" : path.filename().string();
        ImGui::PushID(id.c_str());
        ImGui::GetStateStorage()->SetInt(ImGui::GetID(title.c_str()), expanded ? 1 : 0);

        std::error_code error;
        for (std::filesystem::directory_iterator it(path, error), end; !error && it != end;
             it.increment(error))
        {
            if (!it->is_directory(error) || error)
            {
                error.clear();
                continue;
            }
            const auto candidate = std::filesystem::weakly_canonical(it->path(), error);
            if (!error && IsWithin(m_Root, candidate))
                SetFolderExpansion(candidate, expanded);
            error.clear();
        }
        ImGui::PopID();
        m_FolderTraversal.erase(path);
    }

    void DrawBreadcrumbs()
    {
        ImGui::PushID("AssetBreadcrumbs");
        if (ImGui::SmallButton("Assets"))
            m_Current = m_Root;
        auto relative = m_Current.lexically_relative(m_Root);
        std::filesystem::path accumulated = m_Root;
        for (const auto& part : relative)
        {
            accumulated /= part;
            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();
            if (ImGui::SmallButton(part.string().c_str()))
                m_Current = accumulated;
        }
        ImGui::PopID();
    }

    void DrawEntries(EditorPanelContext& context,
                     const std::vector<std::filesystem::directory_entry>& entries)
    {
        if (!ImGui::BeginTable("AssetRowsTable", 1,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_NoPadOuterX |
                                   ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }
        ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& entry : entries)
        {
            if (entry.is_regular_file() && entry.path().extension() == ".asset")
                continue;

            const auto path = entry.path();
            std::error_code error;
            const bool directory = entry.is_directory(error);
            if (error)
                continue;

            const std::string name = path.filename().string();
            if (!m_Filter.empty() && Lower(name).find(Lower(m_Filter.data())) == std::string::npos)
            {
                continue;
            }

            ImGui::TableNextRow(0, 34.0F);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(path.generic_string().c_str());
            {
                const bool selected = context.SelectedAsset == path;
                const bool clicked = ImGui::Selectable("##asset-row", selected,
                                                       ImGuiSelectableFlags_SpanAllColumns |
                                                           ImGuiSelectableFlags_AllowDoubleClick,
                                                       ImVec2(0.0F, 34.0F));
                const bool hovered = ImGui::IsItemHovered();
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const float iconSize = 22.0F;
                const ImVec2 iconOrigin(rowMin.x + 6.0F,
                                        rowMin.y + (rowMax.y - rowMin.y - iconSize) * 0.5F);
                if (!directory && context.Resources != nullptr && IsImagePath(path))
                {
                    auto image = m_ThumbnailCache.Get(*context.Resources, path);
                    if (image && (*image)->Width > 0 && (*image)->Height > 0)
                    {
                        const GLuint texture = GetTexture(path, **image);
                        const float scale =
                            std::min(iconSize / static_cast<float>((*image)->Width),
                                     iconSize / static_cast<float>((*image)->Height));
                        const ImVec2 imageSize(static_cast<float>((*image)->Width) * scale,
                                               static_cast<float>((*image)->Height) * scale);
                        const ImVec2 imagePosition(iconOrigin.x + (iconSize - imageSize.x) * 0.5F,
                                                   iconOrigin.y + (iconSize - imageSize.y) * 0.5F);
                        drawList->AddImage(
                            static_cast<ImTextureID>(texture), imagePosition,
                            ImVec2(imagePosition.x + imageSize.x, imagePosition.y + imageSize.y));
                    }
                    else
                    {
                        DrawFileIconAt(path, false, iconOrigin, ImVec2(iconSize, iconSize));
                    }
                }
                else
                {
                    DrawFileIconAt(path, directory, iconOrigin, ImVec2(iconSize, iconSize));
                }

                const float textY =
                    rowMin.y + (rowMax.y - rowMin.y - ImGui::GetTextLineHeight()) * 0.5F;
                const ImVec2 namePosition(rowMin.x + 36.0F, textY);
                const std::string type =
                    directory ? "Folder"
                              : (path.extension().empty() ? "File" : path.extension().string());
                const ImVec2 typeSize = ImGui::CalcTextSize(type.c_str());
                const float rightPadding = 10.0F;
                const float typeX =
                    std::max(namePosition.x + 12.0F, rowMax.x - typeSize.x - rightPadding);
                drawList->PushClipRect(namePosition, ImVec2(typeX - 8.0F, rowMax.y), true);
                drawList->AddText(namePosition, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                drawList->PopClipRect();
                drawList->AddText(ImVec2(typeX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                  type.c_str());

                if (clicked)
                    context.SelectedAsset = path;
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (directory)
                        NavigateTo(path, context);
                    else
                    {
                        auto opened = context.OpenFile(path);
                        context.Status = opened ? "Opened " + name : ErrorMessage(opened.error());
                    }
                }
                DrawAssetContextMenu(path, directory, name, context);
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s\n%s", path.filename().string().c_str(),
                                  path.extension().string().c_str());
            }
        }
        ImGui::EndTable();
    }

    void NavigateTo(const std::filesystem::path& path, EditorPanelContext& context)
    {
        std::error_code error;
        const auto target = std::filesystem::weakly_canonical(path, error);
        if (error || !IsWithin(m_Root, target))
        {
            context.Status = "Folder resolves outside the asset root";
            return;
        }
        m_Current = target;
    }

    std::filesystem::path m_Root;
    std::filesystem::path m_Current;
    std::vector<std::filesystem::directory_entry> m_Entries;
    std::chrono::steady_clock::time_point m_LastScan{};
    EditorThumbnailCache m_ThumbnailCache;
    EditorAssetWatcher m_Watcher;
    std::unordered_map<std::filesystem::path, GLuint> m_GpuTextures;
    std::unordered_map<std::filesystem::path, std::filesystem::file_time_type> m_ModifiedTimes;
    std::unordered_map<std::filesystem::path, std::vector<std::filesystem::path>> m_FolderCache;
    std::unordered_set<std::filesystem::path> m_FolderTraversal;
    std::string m_ScanError;
    std::array<char, 128> m_Filter{};
    std::filesystem::path m_RenamePath;
    std::array<char, 256> m_RenameBuffer{};
    bool m_OpenRenamePopup = false;

    static bool IsImagePath(const std::filesystem::path& path)
    {
        const auto extension = Lower(path.extension().string());
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
               extension == ".bmp" || extension == ".tga" || extension == ".gif";
    }

    GLuint GetTexture(const std::filesystem::path& path, const EditorImage& image)
    {
        if (const auto it = m_GpuTextures.find(path); it != m_GpuTextures.end())
            return it->second;
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.Width, image.Height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.Rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        m_GpuTextures.emplace(path, texture);
        return texture;
    }
};

} // namespace

std::unique_ptr<EditorPanel> CreateAssetBrowserPanel()
{
    return std::make_unique<AssetBrowserPanel>();
}
