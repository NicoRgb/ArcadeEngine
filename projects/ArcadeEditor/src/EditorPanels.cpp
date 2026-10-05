#include "ArcadeEditor/EditorPanel.hpp"
#include "ArcadeEditor/EditorTheme.hpp"
#include "EditorPanelFactories.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <stdexcept>
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

namespace
{
using json = nlohmann::json;

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

int ResizeTextBuffer(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
        data->BufSize = static_cast<int>(text->capacity() + 1);
    }
    return 0;
}

bool InputMultiline(std::string& text, const char* id, const ImVec2& size)
{
    return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, size,
                                     ImGuiInputTextFlags_AllowTabInput |
                                         ImGuiInputTextFlags_CallbackResize,
                                     ResizeTextBuffer, &text);
}

class CodeFileEditor : public EditorFileEditor
{
public:
    void OnDocumentClosed(const EditorDocument* document) override
    {
        m_EditSnapshots.erase(document);
    }

protected:
    void DrawCode(EditorPanelContext& context, const std::shared_ptr<EditorDocument>& document,
                  const char* documentKind, unsigned int color)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0F);
        ImGui::TextColored(Color(color), "%s", documentKind);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", document->Path().filename().string().c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 88.0F);
        if (ImGui::SmallButton("Save  Ctrl+S"))
        {
            auto saved = document->Save();
            if (saved)
            {
                context.Status = "Saved " + document->Path().filename().string();
            }
            else
            {
                context.Status = ErrorMessage(saved.error());
            }
        }
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(0x141a23));
        ImGui::PushStyleColor(ImGuiCol_Text, Color(0xdce5f3));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0F, 7.0F));
        if (context.CodeFont != nullptr)
        {
            ImGui::PushFont(context.CodeFont);
        }
        const std::string snapshot = document->Text();
        const bool changed =
            InputMultiline(document->MutableText(), "##document-source",
                           ImVec2(-1.0F, -ImGui::GetFrameHeightWithSpacing() - 2.0F));
        if (ImGui::IsItemActivated())
        {
            m_EditSnapshots[document.get()] = snapshot;
        }
        if (changed)
        {
            context.Status = "Modified " + document->Path().filename().string();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const auto entry = m_EditSnapshots.find(document.get());
            if (entry != m_EditSnapshots.end() && entry->second != document->Text())
            {
                auto recorded = context.History.PushApplied(
                    std::make_unique<TextEditCommand>(document, entry->second, document->Text()));
                if (!recorded)
                {
                    context.Status = ErrorMessage(recorded.error());
                }
            }
            m_EditSnapshots.erase(document.get());
        }
        if (context.CodeFont != nullptr)
        {
            ImGui::PopFont();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    void ClearSnapshot(const EditorDocument* document) { m_EditSnapshots.erase(document); }

private:
    std::unordered_map<const EditorDocument*, std::string> m_EditSnapshots;
};

class TextFileEditor final : public CodeFileEditor
{
public:
    std::string_view Id() const noexcept override { return "text"; }
    void Draw(EditorPanelContext& context, const std::shared_ptr<EditorDocument>& document) override
    {
        DrawCode(context, document, "TEXT DOCUMENT", 0x8e9bac);
    }
};

class ShaderFileEditor final : public CodeFileEditor
{
public:
    std::string_view Id() const noexcept override { return "shader"; }
    void Draw(EditorPanelContext& context, const std::shared_ptr<EditorDocument>& document) override
    {
        ImGui::TextDisabled("Shader source · compilation can be registered as an editor command");
        DrawCode(context, document, "SHADER SOURCE", 0x9f82ff);
    }
};

class JsonFileEditor final : public CodeFileEditor
{
public:
    std::string_view Id() const noexcept override { return "json"; }
    void Draw(EditorPanelContext& context, const std::shared_ptr<EditorDocument>& document) override
    {
        if (ImGui::SmallButton("Format JSON"))
        {
            try
            {
                const std::string before = document->Text();
                auto formatted = json::parse(before).dump(2) + "\n";
                bool success = true;
                if (formatted != before)
                {
                    auto formattedResult = context.History.Execute(
                        std::make_unique<TextEditCommand>(document, before, std::move(formatted)));
                    if (!formattedResult)
                    {
                        success = false;
                        context.Status = ErrorMessage(formattedResult.error());
                    }
                }
                ClearSnapshot(document.get());
                if (success)
                    context.Status = "JSON formatted";
            }
            catch (const json::parse_error& exception)
            {
                context.Status = std::string("Can't format invalid JSON: ") + exception.what();
            }
        }
        ImGui::SameLine();
        try
        {
            (void)json::parse(document->Text());
            ImGui::TextColored(Color(0x66d6a8), "Valid JSON");
        }
        catch (const json::parse_error& exception)
        {
            ImGui::TextColored(Color(0xff8279), "Invalid JSON: %.90s", exception.what());
        }
        DrawCode(context, document, "JSON DOCUMENT", 0x4fc7ac);
    }
};

class WorkspacePanel final : public EditorPanel
{
public:
    std::string_view Id() const noexcept override { return "workspace"; }
    std::string_view Title() const noexcept override { return "Workspace"; }

    void Draw(EditorPanelContext& context) override
    {
        const auto documents = context.Documents.Documents();
        if (documents.empty())
        {
            context.SelectedDocument.reset();
            ImGui::Spacing();
            ImGui::TextColored(Color(0x9caec3), "  Choose an asset to begin editing.");
            ImGui::TextDisabled("  JSON configuration and shader files open here as docked tabs.");
            return;
        }

        if (ImGui::BeginTabBar("EditorDocuments",
                               ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
        {
            for (const auto& document : documents)
            {
                const std::string title =
                    document->Path().filename().string() + (document->IsDirty() ? " *" : "");
                bool open = true;
                if (ImGui::BeginTabItem(title.c_str(), &open))
                {
                    context.SelectedDocument = document;
                    context.FileEditors.Draw(context, document);
                    ImGui::EndTabItem();
                }
                if (!open)
                {
                    if (document->IsDirty())
                    {
                        m_PendingClose = document;
                        m_OpenCloseDialog = true;
                    }
                    else
                    {
                        CloseDocument(context, document);
                    }
                }
            }
            ImGui::EndTabBar();
        }
        if (m_OpenCloseDialog)
        {
            ImGui::OpenPopup("Unsaved Document");
            m_OpenCloseDialog = false;
        }
        if (ImGui::BeginPopupModal("Unsaved Document", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Save changes to %s before closing?",
                        m_PendingClose->Path().filename().string().c_str());
            if (ImGui::Button("Save and close"))
            {
                auto saved = m_PendingClose->Save();
                if (saved)
                {
                    const auto filename = m_PendingClose->Path().filename().string();
                    CloseDocument(context, m_PendingClose);
                    context.Status = "Saved and closed " + filename;
                    m_PendingClose.reset();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    context.Status = ErrorMessage(saved.error());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard changes"))
            {
                m_PendingClose->DiscardChanges();
                context.History.ForgetDocument(m_PendingClose.get());
                CloseDocument(context, m_PendingClose);
                context.Status = "Discarded edits and closed the document";
                m_PendingClose.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep editing"))
            {
                m_PendingClose.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

private:
    static void CloseDocument(EditorPanelContext& context,
                              const std::shared_ptr<EditorDocument>& document)
    {
        context.FileEditors.OnDocumentClosed(document.get());
        (void)context.Documents.Close(document);
        if (context.SelectedDocument == document)
        {
            context.SelectedDocument.reset();
        }
    }

    std::shared_ptr<EditorDocument> m_PendingClose;
    bool m_OpenCloseDialog = false;
};

class InspectorPanel final : public EditorPanel
{
public:
    std::string_view Id() const noexcept override { return "inspector"; }
    std::string_view Title() const noexcept override { return "Inspector"; }

    void Draw(EditorPanelContext& context) override
    {
        ImGui::TextColored(Color(0x88a0be), "SELECTION");
        ImGui::Separator();
        if (context.SelectedDocument)
        {
            ImGui::Text("%s", context.SelectedDocument->Path().filename().string().c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Location");
            ImGui::TextWrapped("%s", context.SelectedDocument->Path().generic_string().c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Document");
            ImGui::BulletText("%s", context.SelectedDocument->IsDirty() ? "Modified" : "Saved");
            if (context.SelectedDocument->HasExternalConflict())
            {
                ImGui::TextColored(Color(0xff8279), "Changed on disk; edits are preserved.");
                if (ImGui::SmallButton("Reload from disk"))
                {
                    auto reloaded = context.SelectedDocument->ReloadFromDisk();
                    context.Status =
                        reloaded ? "Reloaded external changes" : ErrorMessage(reloaded.error());
                    if (reloaded)
                        context.History.ForgetDocument(context.SelectedDocument.get());
                }
            }
            ImGui::BulletText("%zu bytes", context.SelectedDocument->Text().size());
        }
        else if (!context.SelectedAsset.empty())
        {
            ImGui::Text("%s", context.SelectedAsset.filename().string().c_str());
            ImGui::TextDisabled("%s", context.SelectedAsset.extension().string().c_str());
            ImGui::TextWrapped("%s", context.SelectedAsset.generic_string().c_str());
        }
        else
        {
            ImGui::TextDisabled("Select an asset or document to inspect it.");
        }
    }
};

class ConsolePanel final : public EditorPanel
{
public:
    std::string_view Id() const noexcept override { return "console"; }
    std::string_view Title() const noexcept override { return "Console"; }

    void Draw(EditorPanelContext& context) override
    {
        if (context.Logs == nullptr)
        {
            ImGui::TextDisabled("The editor log sink is unavailable.");
            return;
        }
        ImGui::SetNextItemWidth(190.0F);
        ImGui::InputTextWithHint("##console-filter", "Search log...", m_Filter.data(),
                                 m_Filter.size());
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warn", &m_ShowWarn);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_ShowError);
        ImGui::SameLine();
        ImGui::Checkbox("Debug", &m_ShowDebug);
        ImGui::SameLine();
        if (ImGui::Checkbox("Pause", &m_Paused))
        {
            m_PausedEntries =
                m_Paused && context.Logs ? context.Logs->Snapshot() : std::vector<EditorLogEntry>{};
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
            context.Logs->Clear();
        ImGui::Separator();

        if (ImGui::BeginChild("ConsoleEntries", ImVec2(0, 0), ImGuiChildFlags_None))
        {
            const auto entries = m_Paused ? m_PausedEntries : context.Logs->Snapshot();
            for (const auto& entry : entries)
            {
                const bool visible = (entry.Level == LogLevel::Debug && m_ShowDebug) ||
                                     (entry.Level == LogLevel::Info && m_ShowInfo) ||
                                     (entry.Level == LogLevel::Warn && m_ShowWarn) ||
                                     (entry.Level == LogLevel::Error && m_ShowError);
                if (!visible ||
                    (!m_Filter.empty() &&
                     Lower(entry.Message).find(Lower(m_Filter.data())) == std::string::npos))
                {
                    continue;
                }
                const unsigned int tint = entry.Level == LogLevel::Error   ? 0xff817c
                                          : entry.Level == LogLevel::Warn  ? 0xe7b75f
                                          : entry.Level == LogLevel::Debug ? 0x8592a5
                                                                           : 0x72c9a4;
                ImGui::PushStyleColor(ImGuiCol_Text, Color(tint));
                ImGui::TextUnformatted(entry.Message.c_str());
                ImGui::PopStyleColor();
                if (ImGui::BeginPopupContextItem("LogEntry"))
                {
                    if (ImGui::MenuItem("Copy message"))
                        ImGui::SetClipboardText(entry.Message.c_str());
                    ImGui::EndPopup();
                }
            }
            if (!m_Paused && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F)
            {
                ImGui::SetScrollHereY(1.0F);
            }
        }
        ImGui::EndChild();
    }

private:
    std::array<char, 160> m_Filter{};
    bool m_ShowInfo = true;
    bool m_ShowWarn = true;
    bool m_ShowError = true;
    bool m_ShowDebug = false;
    bool m_Paused = false;
    std::vector<EditorLogEntry> m_PausedEntries;
};
} // namespace

Result<std::shared_ptr<EditorDocument>>
EditorPanelContext::OpenFile(const std::filesystem::path& path)
{
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(AssetRoot, error);
    if (error)
    {
        return MakeError(Error::IoFailure, "Unable to resolve asset root: " + AssetRoot.string(),
                         error);
    }
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error || !IsWithin(root, resolved))
    {
        return MakeError(Error::InvalidArgument, "Asset path resolves outside the asset root.",
                         error);
    }

    auto opened = Documents.Open(resolved);
    if (!opened)
    {
        return MakeError(opened.error());
    }
    auto document = std::move(*opened);
    SelectedAsset = resolved;
    SelectedDocument = document;
    Status = "Opened " + resolved.filename().string();
    return document;
}

void EditorFileEditorRegistry::SetFallback(std::unique_ptr<EditorFileEditor> editor)
{
    if (!editor || m_Fallback)
    {
        throw std::invalid_argument("The default file editor must be set once to a valid editor.");
    }
    for (const auto& existing : m_Editors)
    {
        if (existing->Id() == editor->Id())
        {
            throw std::invalid_argument("Editor file editor ids must be unique.");
        }
    }
    m_Fallback = std::move(editor);
}

void EditorFileEditorRegistry::Register(std::vector<std::string> extensions,
                                        std::unique_ptr<EditorFileEditor> editor)
{
    if (!editor || extensions.empty())
    {
        throw std::invalid_argument(
            "A file editor needs an implementation and at least one extension.");
    }
    if (m_Fallback && m_Fallback->Id() == editor->Id())
    {
        throw std::invalid_argument("Editor file editor ids must be unique.");
    }
    for (const auto& existing : m_Editors)
    {
        if (existing->Id() == editor->Id())
        {
            throw std::invalid_argument("Editor file editor ids must be unique.");
        }
    }

    std::vector<std::string> normalizedExtensions;
    normalizedExtensions.reserve(extensions.size());
    for (auto& extension : extensions)
    {
        extension = Lower(std::move(extension));
        if (extension.size() < 2 || extension.front() != '.' ||
            std::ranges::find(normalizedExtensions, extension) != normalizedExtensions.end() ||
            m_ByExtension.contains(extension))
        {
            throw std::invalid_argument(
                "File editor extensions must be unique and begin with a dot.");
        }
        normalizedExtensions.push_back(std::move(extension));
    }

    EditorFileEditor* editorPointer = editor.get();
    m_Editors.push_back(std::move(editor));
    for (auto& extension : normalizedExtensions)
    {
        m_ByExtension.emplace(std::move(extension), editorPointer);
    }
}

void EditorFileEditorRegistry::Draw(EditorPanelContext& context,
                                    const std::shared_ptr<EditorDocument>& document)
{
    if (!document)
    {
        throw std::invalid_argument("Cannot draw an empty editor document.");
    }
    const auto editor = m_ByExtension.find(Lower(document->Path().extension().string()));
    EditorFileEditor* implementation =
        editor == m_ByExtension.end() ? m_Fallback.get() : editor->second;
    if (implementation == nullptr)
    {
        throw std::logic_error("The editor file editor registry has no fallback editor.");
    }
    implementation->Draw(context, document);
}

void EditorFileEditorRegistry::OnDocumentClosed(const EditorDocument* document)
{
    if (m_Fallback)
    {
        m_Fallback->OnDocumentClosed(document);
    }
    for (const auto& editor : m_Editors)
    {
        editor->OnDocumentClosed(document);
    }
}

void EditorPanelRegistry::Register(std::unique_ptr<EditorPanel> panel,
                                   bool hasInitialDockAssignment)
{
    if (!panel)
    {
        throw std::invalid_argument("Cannot register an empty editor panel.");
    }
    for (const auto& existing : m_Panels)
    {
        if (existing->Id() == panel->Id() || existing->Title() == panel->Title())
        {
            throw std::invalid_argument("Editor panel ids and titles must be unique.");
        }
    }
    panel->m_HasInitialDockAssignment = hasInitialDockAssignment;
    m_Panels.push_back(std::move(panel));
}

void EditorPanelRegistry::Draw(EditorPanelContext& context)
{
    for (const auto& panel : m_Panels)
    {
        if (!panel->IsOpen())
        {
            continue;
        }
        bool open = panel->IsOpen();
        if (!panel->m_HasInitialDockAssignment && context.DefaultPanelDockId != 0)
        {
            ImGui::SetNextWindowDockID(context.DefaultPanelDockId, ImGuiCond_FirstUseEver);
        }
        if (ImGui::Begin(std::string(panel->Title()).c_str(), &open))
        {
            panel->Draw(context);
        }
        ImGui::End();
        panel->SetOpen(open);
    }
}

void EditorPanelRegistry::DrawVisibilityMenu()
{
    if (ImGui::BeginMenu("Panels"))
    {
        for (const auto& panel : m_Panels)
        {
            bool open = panel->IsOpen();
            if (ImGui::MenuItem(std::string(panel->Title()).c_str(), nullptr, &open))
            {
                panel->SetOpen(open);
            }
        }
        ImGui::EndMenu();
    }
}

EditorPanel* EditorPanelRegistry::Find(std::string_view id) noexcept
{
    const auto entry =
        std::ranges::find_if(m_Panels, [id](const auto& panel) { return panel->Id() == id; });
    return entry == m_Panels.end() ? nullptr : entry->get();
}

EditorApplicationFactory::EditorApplicationFactory()
{
    m_PanelFactories = {[] { return CreateAssetBrowserPanel(); },
                        [] { return std::make_unique<WorkspacePanel>(); },
                        [] { return std::make_unique<InspectorPanel>(); },
                        [] { return std::make_unique<ConsolePanel>(); }};
    m_FileEditorFactories = {
        []
        {
            return std::pair<std::vector<std::string>, std::unique_ptr<EditorFileEditor>>{
                {}, std::make_unique<TextFileEditor>()};
        },
        []
        {
            return std::pair<std::vector<std::string>, std::unique_ptr<EditorFileEditor>>{
                {".hlsl", ".glsl", ".shader", ".vert", ".frag", ".comp"},
                std::make_unique<ShaderFileEditor>()};
        },
        []
        {
            return std::pair<std::vector<std::string>, std::unique_ptr<EditorFileEditor>>{
                {".json"}, std::make_unique<JsonFileEditor>()};
        }};
}

Result<void> EditorApplicationFactory::RegisterPanel(PanelFactory factory)
{
    if (!factory)
    {
        return MakeError(Error::InvalidArgument, "Panel factory cannot be empty.");
    }
    m_PanelFactories.push_back(std::move(factory));
    return {};
}

Result<void> EditorApplicationFactory::RegisterFileEditor(FileEditorFactory factory)
{
    if (!factory)
    {
        return MakeError(Error::InvalidArgument, "File editor factory cannot be empty.");
    }
    m_FileEditorFactories.push_back(std::move(factory));
    return {};
}

EditorPanelRegistry EditorApplicationFactory::CreatePanels() const
{
    EditorPanelRegistry registry;
    for (const auto& factory : m_PanelFactories)
    {
        auto panel = factory();
        const bool hasDockSlot =
            panel && (panel->Id() == "asset-browser" || panel->Id() == "workspace" ||
                      panel->Id() == "inspector" || panel->Id() == "console");
        registry.Register(std::move(panel), hasDockSlot);
    }
    return registry;
}

EditorFileEditorRegistry EditorApplicationFactory::CreateFileEditors() const
{
    EditorFileEditorRegistry registry;
    for (const auto& factory : m_FileEditorFactories)
    {
        auto [extensions, editor] = factory();
        if (extensions.empty())
            registry.SetFallback(std::move(editor));
        else
            registry.Register(std::move(extensions), std::move(editor));
    }
    return registry;
}
