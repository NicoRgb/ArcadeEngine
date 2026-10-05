#pragma once

#include "ArcadeEditor/EditorCommand.hpp"
#include "ArcadeEditor/EditorDocument.hpp"
#include "ArcadeEditor/EditorPanel.hpp"
#include "ArcadeEditor/EditorTheme.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <string>

class Application;
class Window;
class EditorDockspaceBuilder;

class EditorApplication
{
public:
    explicit EditorApplication(EditorApplicationFactory factory = {});
    ~EditorApplication();

    EditorApplication(const EditorApplication&) = delete;
    EditorApplication& operator=(const EditorApplication&) = delete;

    void RegisterPanel(std::unique_ptr<EditorPanel> panel);
    void RegisterFileEditor(std::vector<std::string> extensions,
                            std::unique_ptr<EditorFileEditor> editor);
    int Run();

private:
    void OnSetup();
    void OnInit();
    void OnUpdate();
    void OnShutdown() noexcept;
    void DrawMenuBar();
    void DrawDockspace();
    void DrawProjectLauncher();
    [[nodiscard]] Result<void> SetProject(EditorProject project);
    void UpdateRecoverySnapshot();
    void RequestExit();
    [[nodiscard]] bool HasUnsavedChanges() const noexcept;

    std::unique_ptr<Application> m_EngineApplication;
    std::unique_ptr<Window> m_Window;
    std::shared_ptr<EditorLogBuffer> m_LogBuffer;
    Logger m_EditorLogger;
    EditorProjectService m_ProjectService;
    EditorThemeManager m_ThemeManager;
    EditorDocumentStore m_Documents;
    UndoRedoStack m_History;
    EditorFileEditorRegistry m_FileEditors;
    EditorPanelContext m_PanelContext;
    EditorPanelRegistry m_Panels;
    std::unique_ptr<EditorDockspaceBuilder> m_DockspaceBuilder;
    bool m_SeedLayout = false;
    bool m_ShowExitDialog = false;
    bool m_ImGuiContextCreated = false;
    bool m_GlfwBackendInitialized = false;
    bool m_RendererBackendInitialized = false;
    bool m_ShowProjectLauncher = false;
    bool m_ShowSettings = false;
    bool m_ShowCommandPalette = false;
    bool m_ShowRecoveryDialog = false;
    bool m_RecoveryDecisionPending = false;
    std::chrono::steady_clock::time_point m_LastRecoverySave{};
    std::array<char, 512> m_ProjectPathInput{};
    std::array<char, 256> m_ProjectNameInput{};
    std::array<char, 96> m_CommandFilter{};
    std::string m_LastProjectError;
    std::string m_ProjectLayoutPath;
    std::filesystem::path m_RecoveryFilePath;
};
