#pragma once

struct WindowSpec
{
    std::string_view Title;
    uint32_t Width = 1280;
    uint32_t Height = 720;

    bool Fullscreen = false;
    bool Resizable = true;
    bool Maximized = false;
    bool FocusOnShow = true;
};

class WindowSubsystem
{
public:
    static void Init();
    static void Shutdown();
};

class Window
{
public:
    explicit Window(const WindowSpec& spec);
    ~Window();

private:
    void* m_NativeWindow;
};
