#include "Application/Application.hpp"
#include "Application/GraphicsDevice.hpp"
#include "Application/Window.hpp"

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#if defined(ARCADE_ENABLE_TRACY)
#    include <tracy/Tracy.hpp>
#endif

int main()
{
    Application application;
    Window window({.Title = "Arcade Editor"});
    GraphicsDevice graphicsDevice(window);

    auto* nativeWindow = static_cast<GLFWwindow*>(window.NativeHandle());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    unsigned char* fontAtlasPixels = nullptr;
    int fontAtlasWidth = 0;
    int fontAtlasHeight = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(
        &fontAtlasPixels, &fontAtlasWidth, &fontAtlasHeight);
    if (!ImGui_ImplGlfw_InitForOther(nativeWindow, true))
    {
        ImGui::DestroyContext();
        return 1;
    }

    while (!window.ShouldClose())
    {
        WindowSubsystem::PollEvents();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::ShowDemoWindow();
        ImGui::Render();

#if defined(ARCADE_ENABLE_TRACY)
        FrameMark;
#endif

        WindowSubsystem::WaitEventsTimeout(1.0 / 60.0);
    }

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    (void)graphicsDevice.WaitForIdle();
    return 0;
}
