#pragma once

class Application
{
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    static Application& Get() { return *s_Instance; }

    void Run();

private:
    static Application* s_Instance;
};
