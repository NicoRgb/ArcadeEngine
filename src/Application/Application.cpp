#include "Application/Application.hpp"
#include <tracy/Tracy.hpp>

Application* Application::s_Instance = nullptr;

Application::Application()
{
    s_Instance = this;
}

Application::~Application()
{
    s_Instance = nullptr;
}

void Application::Run()
{
    while (true)
    {
        FrameMark;
    }
}
