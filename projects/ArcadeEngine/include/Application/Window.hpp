#pragma once

#include <cstdint>
#include <string_view>

#include "Core/Export.hpp"

struct WindowSpec
{
    std::string_view Title = "Arcade";
    uint32_t Width = 1280;
    uint32_t Height = 720;

    bool Fullscreen = false;
    bool Resizable = true;
    bool Maximized = false;
    bool FocusOnShow = true;
};

class ARCADE_ENGINE_API WindowSubsystem
{
public:
    static void PollEvents();
    static void WaitEventsTimeout(double timeoutSeconds);
};

class ARCADE_ENGINE_API Window
{
public:
    explicit Window(const WindowSpec& spec);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool ShouldClose() const noexcept;
    void RequestClose() noexcept;
    [[nodiscard]] void* NativeHandle() const noexcept { return m_NativeWindow; }

private:
    void* m_NativeWindow = nullptr;
};
