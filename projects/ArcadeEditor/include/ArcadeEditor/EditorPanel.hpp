#pragma once

#include "ArcadeEditor/EditorCommand.hpp"
#include "ArcadeEditor/EditorDocument.hpp"
#include "ArcadeEditor/EditorLogBuffer.hpp"
#include "ArcadeEditor/EditorProject.hpp"
#include "Core/Resource.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

struct ImFont;
class EditorFileEditorRegistry;

struct EditorPanelContext
{
    EditorDocumentStore& Documents;
    UndoRedoStack& History;
    EditorFileEditorRegistry& FileEditors;
    std::filesystem::path AssetRoot;
    ResourceManager* Resources = nullptr;
    EditorLogBuffer* Logs = nullptr;
    std::optional<EditorProject> Project;
    std::shared_ptr<EditorDocument> SelectedDocument;
    std::filesystem::path SelectedAsset;
    unsigned int DefaultPanelDockId = 0;
    ImFont* CodeFont = nullptr;
    std::string Status = "Ready";

    [[nodiscard]] Result<std::shared_ptr<EditorDocument>>
    OpenFile(const std::filesystem::path& path);
};

class EditorPanel
{
public:
    virtual ~EditorPanel() = default;
    [[nodiscard]] virtual std::string_view Id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view Title() const noexcept = 0;
    virtual void Draw(EditorPanelContext& context) = 0;

    [[nodiscard]] bool IsOpen() const noexcept { return m_Open; }
    void SetOpen(bool open) noexcept { m_Open = open; }

private:
    friend class EditorPanelRegistry;
    bool m_Open = true;
    bool m_HasInitialDockAssignment = false;
};

class EditorPanelRegistry
{
public:
    void Register(std::unique_ptr<EditorPanel> panel, bool hasInitialDockAssignment = false);
    void Draw(EditorPanelContext& context);
    void DrawVisibilityMenu();
    [[nodiscard]] EditorPanel* Find(std::string_view id) noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<EditorPanel>>& Panels() const noexcept
    {
        return m_Panels;
    }

private:
    std::vector<std::unique_ptr<EditorPanel>> m_Panels;
};

class EditorFileEditor
{
public:
    virtual ~EditorFileEditor() = default;
    [[nodiscard]] virtual std::string_view Id() const noexcept = 0;
    virtual void Draw(EditorPanelContext& context,
                      const std::shared_ptr<EditorDocument>& document) = 0;
    virtual void OnDocumentClosed(const EditorDocument*) {}
};

class EditorFileEditorRegistry
{
public:
    void SetFallback(std::unique_ptr<EditorFileEditor> editor);
    void Register(std::vector<std::string> extensions, std::unique_ptr<EditorFileEditor> editor);
    void Draw(EditorPanelContext& context, const std::shared_ptr<EditorDocument>& document);
    void OnDocumentClosed(const EditorDocument* document);

private:
    std::unique_ptr<EditorFileEditor> m_Fallback;
    std::vector<std::unique_ptr<EditorFileEditor>> m_Editors;
    std::unordered_map<std::string, EditorFileEditor*> m_ByExtension;
};

class EditorApplicationFactory
{
public:
    using PanelFactory = std::function<std::unique_ptr<EditorPanel>()>;
    using FileEditorFactory =
        std::function<std::pair<std::vector<std::string>, std::unique_ptr<EditorFileEditor>>()>;

    EditorApplicationFactory();
    [[nodiscard]] Result<void> RegisterPanel(PanelFactory factory);
    [[nodiscard]] Result<void> RegisterFileEditor(FileEditorFactory factory);
    [[nodiscard]] EditorPanelRegistry CreatePanels() const;
    [[nodiscard]] EditorFileEditorRegistry CreateFileEditors() const;

private:
    std::vector<PanelFactory> m_PanelFactories;
    std::vector<FileEditorFactory> m_FileEditorFactories;
};
