#include "Application/Application.hpp"
#include "Application/GraphicsDevice.hpp"
#include "Application/Window.hpp"

int main()
{
    Application application;
    Window window({.Title = "Arcade Runtime"});
    GraphicsDevice graphicsDevice(window);

    while (!window.ShouldClose())
    {
        WindowSubsystem::WaitEventsTimeout(1.0 / 60.0);
    }

    (void)graphicsDevice.WaitForIdle();
    return 0;
}
