#include "ArcadeEditor/EditorApplication.hpp"
#include "ArcadeEditor/EditorDockspaceBuilder.hpp"
#include "ArcadeEditor/EditorTheme.hpp"

#include "Application/Application.hpp"
#include "Application/Window.hpp"
#include "Assets/AssetManager.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#if defined(_WIN32)
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#if defined(ARCADE_ENABLE_TRACY)
#include <tracy/Tracy.hpp>
#endif

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
bool CommandMatches(std::string_view label, std::string_view filter)
{
    auto lower = [](std::string_view text)
    {
        std::string value(text);
        std::ranges::transform(value, value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    return lower(label).find(lower(filter)) != std::string::npos;
}

} // namespace

EditorApplication::EditorApplication(EditorApplicationFactory factory)
    : m_LogBuffer(std::make_shared<EditorLogBuffer>()), m_EditorLogger("ArcadeEditor"),
      m_ProjectService(ArcadeUserConfigDirectory() / "recent-projects.json"),
      m_ThemeManager(ArcadeUserConfigDirectory() / "editor-settings.json"),
      m_FileEditors(factory.CreateFileEditors()),
      m_PanelContext{.Documents = m_Documents,
                     .History = m_History,
                     .FileEditors = m_FileEditors,
                     .AssetRoot = ARCADE_EDITOR_CONTENT_DIR,
                     .Resources = nullptr,
                     .Logs = m_LogBuffer.get()},
      m_Panels(factory.CreatePanels()),
      m_DockspaceBuilder(std::make_unique<EditorDockspaceBuilder>())
{
    m_DockspaceBuilder->SplitArea(ImGuiDir_Left, 0.22F, "Asset Browser")
        .SplitArea(ImGuiDir_Right, 0.24F, "Inspector")
        .SplitArea(ImGuiDir_Down, 0.25F, "Console")
        .CenterPanel("Workspace");
}

EditorApplication::~EditorApplication()
{
    OnShutdown();
}

void EditorApplication::RegisterPanel(std::unique_ptr<EditorPanel> panel)
{
    m_Panels.Register(std::move(panel));
}

void EditorApplication::RegisterFileEditor(std::vector<std::string> extensions,
                                           std::unique_ptr<EditorFileEditor> editor)
{
    m_FileEditors.Register(std::move(extensions), std::move(editor));
}

int EditorApplication::Run()
{
    OnSetup();
    m_EngineApplication = std::make_unique<Application>();
    m_EngineApplication->GetEngineLogger().AddSink(m_LogBuffer);
    m_EditorLogger.AddSink(std::make_shared<StdoutLogSink>());
    m_EditorLogger.AddSink(m_LogBuffer);
    m_PanelContext.Resources = &m_EngineApplication->GetResourceManager();
    m_PanelContext.Assets = &m_EngineApplication->GetAssetManager();
    m_Window = std::make_unique<Window>(WindowSpec{.Title = "Arcade Editor",
                                                   .Width = 1600,
                                                   .Height = 1000,
                                                   .Maximized = true,
                                                   .ClientApi = WindowClientApi::OpenGL});
    const auto defaultProject =
        std::filesystem::path(ARCADE_EDITOR_CONTENT_DIR) / "ArcadeProject.json";
    auto project = m_ProjectService.Open(defaultProject);
    if (project)
    {
        auto activated = SetProject(std::move(*project));
        if (!activated)
            m_LastProjectError = ErrorMessage(activated.error());
    }
    else
    {
        m_ShowProjectLauncher = true;
        m_LastProjectError = ErrorMessage(project.error());
    }
    OnInit();
    while (!m_Window->ShouldClose())
    {
        OnUpdate();
    }
    OnShutdown();
    return 0;
}

void EditorApplication::OnSetup()
{
    m_SeedLayout = true;
}

void EditorApplication::OnInit()
{
    auto* nativeWindow = static_cast<GLFWwindow*>(m_Window->NativeHandle());
    glfwMakeContextCurrent(nativeWindow);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_ImGuiContextCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    m_ProjectLayoutPath =
        m_PanelContext.Project
            ? (m_PanelContext.Project->Root / ".arcade" / "ArcadeEditor.ini").string()
            : (ArcadeUserConfigDirectory() / "default-layout.ini").string();
    std::error_code layoutError;
    std::filesystem::create_directories(std::filesystem::path(m_ProjectLayoutPath).parent_path(),
                                        layoutError);
    m_SeedLayout = layoutError || !std::filesystem::exists(m_ProjectLayoutPath);
    io.IniFilename = m_ProjectLayoutPath.c_str();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    auto loadedTheme = m_ThemeManager.Load();
    if (!loadedTheme)
    {
        m_EditorLogger.LogWarn("Could not load editor settings: {}",
                               ErrorMessage(loadedTheme.error()));
    }
    m_ThemeManager.Apply();

    const auto assetDirectory = std::filesystem::path(ARCADE_EDITOR_ASSET_DIR);
    ImFont* uiFont = io.Fonts->AddFontFromFileTTF(
        (assetDirectory / "fonts/Roboto-Medium.ttf").string().c_str(), 17.0F);
    ImFont* codeFont = io.Fonts->AddFontFromFileTTF(
        (assetDirectory / "fonts/Cousine-Regular.ttf").string().c_str(), 15.0F);
    if (uiFont == nullptr)
    {
        uiFont = io.Fonts->AddFontDefault();
        std::cerr << "ArcadeEditor: Roboto font is missing; using the ImGui default font.\n";
    }
    if (codeFont == nullptr)
    {
        codeFont = uiFont;
        std::cerr << "ArcadeEditor: Cousine font is missing; using the UI font for code.\n";
    }
    io.FontDefault = uiFont;
    m_PanelContext.CodeFont = codeFont;

    if (!ImGui_ImplGlfw_InitForOpenGL(nativeWindow, true))
    {
        throw std::runtime_error("Dear ImGui GLFW backend initialization failed.");
    }
    m_GlfwBackendInitialized = true;
    if (!ImGui_ImplOpenGL3_Init("#version 150"))
    {
        throw std::runtime_error("Dear ImGui OpenGL backend initialization failed.");
    }
    m_RendererBackendInitialized = true;
}

void EditorApplication::OnUpdate()
{
    if (m_PanelContext.Project && !m_RecoveryDecisionPending &&
        (m_LastRecoverySave.time_since_epoch().count() == 0 ||
         std::chrono::steady_clock::now() - m_LastRecoverySave > std::chrono::seconds(5)))
    {
        UpdateRecoverySnapshot();
    }
    WindowSubsystem::PollEvents();
    const bool closeObserved = m_Window->ShouldClose();
    if (closeObserved && HasUnsavedChanges())
    {
        m_Window->CancelCloseRequest();
        m_ShowExitDialog = true;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        auto saved =
            io.KeyShift ? m_Documents.SaveAll()
                        : (m_PanelContext.SelectedDocument ? m_PanelContext.SelectedDocument->Save()
                                                           : Result<void>{});
        if (saved)
        {
            m_PanelContext.Status =
                io.KeyShift ? "Saved all open documents"
                : m_PanelContext.SelectedDocument
                    ? "Saved " + m_PanelContext.SelectedDocument->Path().filename().string()
                    : "No active document to save";
        }
        else
        {
            m_PanelContext.Status = ErrorMessage(saved.error());
            m_EditorLogger.LogError("Save failed: {}", m_PanelContext.Status);
        }
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        auto undone = m_History.Undo();
        if (undone)
        {
            m_PanelContext.Status = "Undo " + m_History.RedoName();
        }
        else
        {
            m_PanelContext.Status = ErrorMessage(undone.error());
        }
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
    {
        auto redone = m_History.Redo();
        if (redone)
        {
            m_PanelContext.Status = "Redo " + m_History.UndoName();
        }
        else
        {
            m_PanelContext.Status = ErrorMessage(redone.error());
        }
    }

    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        m_ShowProjectLauncher = true;
    if (!io.WantTextInput && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P))
    {
        m_ShowCommandPalette = true;
    }
    DrawMenuBar();
    if (m_ShowProjectLauncher)
    {
        DrawProjectLauncher();
    }
    DrawDockspace();
    ImGui::Render();

    auto* nativeWindow = static_cast<GLFWwindow*>(m_Window->NativeHandle());
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(nativeWindow, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.055F, 0.068F, 0.086F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(nativeWindow);

#if defined(ARCADE_ENABLE_TRACY)
    FrameMark;
#endif
}

void EditorApplication::OnShutdown() noexcept
{
    if (m_RendererBackendInitialized)
    {
        ImGui_ImplOpenGL3_Shutdown();
        m_RendererBackendInitialized = false;
    }
    if (m_GlfwBackendInitialized)
    {
        ImGui_ImplGlfw_Shutdown();
        m_GlfwBackendInitialized = false;
    }
    if (m_ImGuiContextCreated)
    {
        ImGui::DestroyContext();
        m_ImGuiContextCreated = false;
    }
}

void EditorApplication::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
            m_ShowProjectLauncher = true;
        if (ImGui::MenuItem("New Project..."))
        {
            m_ShowProjectLauncher = true;
            m_ProjectPathInput.fill('\0');
            m_ProjectNameInput.fill('\0');
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, bool(m_PanelContext.SelectedDocument)))
        {
            auto saved = m_PanelContext.SelectedDocument->Save();
            m_PanelContext.Status = saved ? "Document saved" : ErrorMessage(saved.error());
        }
        if (ImGui::MenuItem("Save All", "Ctrl+Shift+S"))
        {
            auto saved = m_Documents.SaveAll();
            m_PanelContext.Status =
                saved ? "Saved all open documents" : ErrorMessage(saved.error());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
        {
            RequestExit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        ImGui::BeginDisabled(!m_History.CanUndo());
        if (ImGui::MenuItem("Undo", "Ctrl+Z"))
        {
            auto result = m_History.Undo();
            m_PanelContext.Status =
                result ? "Undo " + m_History.RedoName() : ErrorMessage(result.error());
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_History.CanRedo());
        if (ImGui::MenuItem("Redo", "Ctrl+Y"))
        {
            auto result = m_History.Redo();
            m_PanelContext.Status =
                result ? "Redo " + m_History.UndoName() : ErrorMessage(result.error());
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Settings..."))
            m_ShowSettings = true;
        if (ImGui::MenuItem("Command Palette", "Ctrl+Shift+P"))
            m_ShowCommandPalette = true;
        ImGui::EndMenu();
    }
    m_Panels.DrawVisibilityMenu();
    if (ImGui::BeginMenu("Help"))
    {
        ImGui::MenuItem("ArcadeEditor  ·  early access", nullptr, false, false);
        ImGui::EndMenu();
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 330.0F);
    ImGui::TextColored(ImVec4(0.62F, 0.58F, 1.0F, 1.0F), "ARCADE");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_PanelContext.Project ? m_PanelContext.Project->Name.c_str()
                                                     : "NO PROJECT");
    ImGui::EndMainMenuBar();

    if (m_ShowSettings)
    {
        ImGui::SetNextWindowSize(ImVec2(460, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings", &m_ShowSettings))
        {
            ImGui::Text("Appearance");
            int selectedTheme = static_cast<int>(m_ThemeManager.Current());
            const char* themes[] = {"Arcade Graphite", "Light", "High Contrast"};
            ImGui::SetNextItemWidth(230.0F);
            if (ImGui::Combo("Theme", &selectedTheme, themes, IM_ARRAYSIZE(themes)))
            {
                auto saved = m_ThemeManager.Set(static_cast<EditorTheme>(selectedTheme));
                if (!saved)
                    m_PanelContext.Status = ErrorMessage(saved.error());
            }
            ImGui::Separator();
            ImGui::TextWrapped(
                "Themes are saved for this user. Layout and recovery are stored with the project.");
        }
        ImGui::End();
    }

    if (m_ShowCommandPalette)
    {
        ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(480, 280), ImVec2(760, 640));
        if (ImGui::Begin("Command Palette", &m_ShowCommandPalette, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint("##command-filter", "Type a command...",
                                     m_CommandFilter.data(), m_CommandFilter.size());
            if (CommandMatches("Open Project", m_CommandFilter.data()) &&
                ImGui::Selectable("Open Project  ·  Ctrl+O"))
            {
                m_ShowProjectLauncher = true;
                m_ShowCommandPalette = false;
            }
            if (CommandMatches("Settings", m_CommandFilter.data()) && ImGui::Selectable("Settings"))
            {
                m_ShowSettings = true;
                m_ShowCommandPalette = false;
            }
            if (CommandMatches("Save All", m_CommandFilter.data()) &&
                ImGui::Selectable("Save All  ·  Ctrl+Shift+S"))
            {
                auto result = m_Documents.SaveAll();
                m_PanelContext.Status =
                    result ? "Saved all open documents" : ErrorMessage(result.error());
                m_ShowCommandPalette = false;
            }
            if (CommandMatches("Toggle Console", m_CommandFilter.data()) &&
                ImGui::Selectable("Toggle Console"))
            {
                if (auto* panel = m_Panels.Find("console"))
                    panel->SetOpen(!panel->IsOpen());
                m_ShowCommandPalette = false;
            }
        }
        ImGui::End();
    }

    if (m_ShowExitDialog)
    {
        ImGui::OpenPopup("Unsaved Editor Changes");
        m_ShowExitDialog = false;
    }
    const bool exitPopupWasOpen = ImGui::IsPopupOpen("Unsaved Editor Changes");
    bool exitDialogOpen = true;
    if (ImGui::BeginPopupModal("Unsaved Editor Changes", &exitDialogOpen,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Save open document changes before exiting?");
        if (ImGui::Button("Save All and Exit"))
        {
            auto saved = m_Documents.SaveAll();
            if (saved)
            {
                m_Window->RequestClose();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_PanelContext.Status = ErrorMessage(saved.error());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard All and Exit"))
        {
            for (const auto& document : m_Documents.Documents())
            {
                document->DiscardChanges();
            }
            m_History.Clear();
            m_Window->RequestClose();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_Window->CancelCloseRequest();
            ImGui::CloseCurrentPopup();
        }
        if (!exitDialogOpen)
        {
            m_Window->CancelCloseRequest();
            m_ShowExitDialog = false;
        }
        ImGui::EndPopup();
    }
    else if (exitPopupWasOpen && !exitDialogOpen)
    {
        m_Window->CancelCloseRequest();
        m_ShowExitDialog = false;
    }

    if (m_ShowRecoveryDialog)
    {
        ImGui::OpenPopup("Recover Unsaved Work");
        m_ShowRecoveryDialog = false;
    }
    if (ImGui::BeginPopupModal("Recover Unsaved Work", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("ArcadeEditor found unsaved document recovery data for this project.");
        if (ImGui::Button("Restore"))
        {
            auto restored = m_Documents.RestoreRecovery(m_RecoveryFilePath);
            if (restored)
            {
                m_PanelContext.Status =
                    "Restored " + std::to_string(*restored) + " recovered documents";
                m_RecoveryDecisionPending = false;
                ImGui::CloseCurrentPopup();
            }
            else
                m_PanelContext.Status = ErrorMessage(restored.error());
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard recovery"))
        {
            std::error_code error;
            std::filesystem::remove(m_RecoveryFilePath, error);
            m_PanelContext.Status = error ? "Unable to remove recovery data: " + error.message()
                                          : "Recovery data discarded";
            if (!error)
            {
                m_RecoveryDecisionPending = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Later"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

Result<void> EditorApplication::SetProject(EditorProject project)
{
    if (m_PanelContext.Project && m_PanelContext.Project->Root != project.Root &&
        HasUnsavedChanges())
    {
        return MAKE_ERROR_MSG(Error::InvalidState,
                              "Save or discard open edits before switching projects.");
    }
    if (m_PanelContext.Assets != nullptr)
    {
        auto indexed = m_PanelContext.Assets->RescanAssets(project.AssetRoot);
        if (!indexed)
        {
            m_EditorLogger.LogError("Could not index project assets at {}: {}",
                                    project.AssetRoot.string(), ErrorMessage(indexed.error()));
            return FORWARD_ERROR(indexed);
        }
    }
    if (m_PanelContext.Project && m_PanelContext.Project->Root != project.Root)
    {
        for (const auto& document : m_Documents.Documents())
        {
            (void)m_Documents.Close(document);
        }
        m_History.Clear();
        m_PanelContext.SelectedDocument.reset();
        m_PanelContext.SelectedAsset.clear();
    }
    m_PanelContext.Project = std::move(project);
    m_PanelContext.AssetRoot = m_PanelContext.Project->AssetRoot;
    m_ProjectLayoutPath = (m_PanelContext.Project->Root / ".arcade" / "ArcadeEditor.ini").string();
    m_RecoveryFilePath = m_PanelContext.Project->Root / ".arcade" / "recovery.json";
    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui::GetIO().IniFilename = m_ProjectLayoutPath.c_str();
    }
    std::error_code error;
    const auto recoverySize = std::filesystem::file_size(m_RecoveryFilePath, error);
    m_ShowRecoveryDialog = !error && recoverySize > 2;
    m_RecoveryDecisionPending = m_ShowRecoveryDialog;
    m_ShowProjectLauncher = false;
    m_LastRecoverySave = {};
    return {};
}

void EditorApplication::UpdateRecoverySnapshot()
{
    m_LastRecoverySave = std::chrono::steady_clock::now();
    auto saved = m_Documents.SaveRecovery(m_RecoveryFilePath);
    if (!saved)
    {
        m_EditorLogger.LogError("Unable to save recovery data: {}", ErrorMessage(saved.error()));
    }
}

void EditorApplication::DrawProjectLauncher()
{
    ImGui::SetNextWindowSize(ImVec2(680, 500), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5F, 0.5F));
    if (!ImGui::Begin("Project Hub", &m_ShowProjectLauncher, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }
    ImGui::TextColored(ImVec4(0.65F, 0.58F, 1.0F, 1.0F), "ARCADE ENGINE");
    ImGui::TextDisabled("Open a project or create a workspace.");
    ImGui::Separator();
    ImGui::Text("Open project descriptor");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##project-path", "Path to ArcadeProject.json",
                             m_ProjectPathInput.data(), m_ProjectPathInput.size());
    if (ImGui::Button("Open Project") && m_ProjectPathInput[0] != '\0')
    {
        auto project = m_ProjectService.Open(std::filesystem::path(m_ProjectPathInput.data()));
        if (project)
        {
            auto activated = SetProject(std::move(*project));
            if (activated)
                m_LastProjectError.clear();
            else
                m_LastProjectError = ErrorMessage(activated.error());
        }
        else
            m_LastProjectError = ErrorMessage(project.error());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Enter the full path to a project manifest.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Create project");
    ImGui::InputTextWithHint("##project-name", "Project name", m_ProjectNameInput.data(),
                             m_ProjectNameInput.size());
    ImGui::InputTextWithHint("##project-root", "New project folder", m_ProjectPathInput.data(),
                             m_ProjectPathInput.size());
    if (ImGui::Button("Create Project") && m_ProjectNameInput[0] != '\0' &&
        m_ProjectPathInput[0] != '\0')
    {
        auto project = m_ProjectService.Create(std::filesystem::path(m_ProjectPathInput.data()),
                                               m_ProjectNameInput.data());
        if (project)
        {
            auto activated = SetProject(std::move(*project));
            if (activated)
                m_LastProjectError.clear();
            else
                m_LastProjectError = ErrorMessage(activated.error());
        }
        else
            m_LastProjectError = ErrorMessage(project.error());
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Recent projects");
    auto recent = m_ProjectService.RecentProjects();
    if (recent)
    {
        for (const auto& descriptor : *recent)
        {
            const auto label =
                descriptor.parent_path().filename().string() + "  ·  " + descriptor.string();
            if (ImGui::Selectable(label.c_str()))
            {
                auto project = m_ProjectService.Open(descriptor);
                if (project)
                {
                    auto activated = SetProject(std::move(*project));
                    if (activated)
                        m_LastProjectError.clear();
                    else
                        m_LastProjectError = ErrorMessage(activated.error());
                }
                else
                    m_LastProjectError = ErrorMessage(project.error());
            }
        }
    }
    if (!m_LastProjectError.empty())
    {
        ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.40F, 1.0F), "%s", m_LastProjectError.c_str());
    }
    ImGui::End();
}

void EditorApplication::DrawDockspace()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImHashStr("ArcadeEditorDockspace");
    m_DockspaceBuilder->BuildIfNeeded(dockspaceId, *viewport, m_SeedLayout);
    m_SeedLayout = false;
    ImGui::DockSpaceOverViewport(dockspaceId, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
    m_PanelContext.DefaultPanelDockId = m_DockspaceBuilder->CenterNode();
    m_Panels.Draw(m_PanelContext);
    const float statusHeight = ImGui::GetFrameHeight() + 8.0F;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statusHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, statusHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags statusFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    if (ImGui::Begin("##EditorStatusBar", nullptr, statusFlags))
    {
        ImGui::TextColored(ImVec4(0.43F, 0.79F, 0.65F, 1.0F), "●");
        ImGui::SameLine();
        ImGui::TextUnformatted(m_PanelContext.Status.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 180.0F);
        ImGui::TextDisabled("Undo %zu  ·  Redo %zu", m_History.UndoCount(), m_History.RedoCount());
    }
    ImGui::End();
}

void EditorApplication::RequestExit()
{
    if (HasUnsavedChanges())
    {
        m_ShowExitDialog = true;
    }
    else
    {
        m_Window->RequestClose();
    }
}

bool EditorApplication::HasUnsavedChanges() const noexcept
{
    return std::ranges::any_of(m_Documents.Documents(),
                               [](const auto& document) { return document->IsDirty(); });
}
