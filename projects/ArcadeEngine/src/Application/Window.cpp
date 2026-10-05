#include "Application/Window.hpp"

#include <GLFW/glfw3.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace
{
int s_WindowCount = 0;

void ReleaseGlfw()
{
    --s_WindowCount;
    if (s_WindowCount == 0)
    {
        glfwTerminate();
    }
}
} // namespace

void WindowSubsystem::PollEvents()
{
    glfwPollEvents();
}

void WindowSubsystem::WaitEventsTimeout(double timeoutSeconds)
{
    glfwWaitEventsTimeout(timeoutSeconds);
}

Window::Window(const WindowSpec& spec)
{
    if (spec.Width == 0 || spec.Height == 0 ||
        spec.Width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        spec.Height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument("Window dimensions must fit in a positive int.");
    }

    if (s_WindowCount == 0 && glfwInit() != GLFW_TRUE)
    {
        throw std::runtime_error("GLFW initialization failed.");
    }

    ++s_WindowCount;

    glfwDefaultWindowHints();
    if (spec.ClientApi == WindowClientApi::OpenGL)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }
    else
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    glfwWindowHint(GLFW_RESIZABLE, spec.Resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, spec.FocusOnShow ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, spec.Maximized ? GLFW_TRUE : GLFW_FALSE);

    GLFWmonitor* monitor = spec.Fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    const int width = static_cast<int>(spec.Width);
    const int height = static_cast<int>(spec.Height);
    auto* window =
        glfwCreateWindow(width, height, std::string(spec.Title).c_str(), monitor, nullptr);
    if (window == nullptr)
    {
        ReleaseGlfw();
        throw std::runtime_error("GLFW window creation failed.");
    }

    m_NativeWindow = window;
}

Window::~Window()
{
    if (m_NativeWindow != nullptr)
    {
        glfwDestroyWindow(static_cast<GLFWwindow*>(m_NativeWindow));
        m_NativeWindow = nullptr;
        ReleaseGlfw();
    }
}

bool Window::ShouldClose() const noexcept
{
    return m_NativeWindow == nullptr ||
           glfwWindowShouldClose(static_cast<GLFWwindow*>(m_NativeWindow)) == GLFW_TRUE;
}

void Window::RequestClose() noexcept
{
    if (m_NativeWindow != nullptr)
    {
        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(m_NativeWindow), GLFW_TRUE);
    }
}

void Window::CancelCloseRequest() noexcept
{
    if (m_NativeWindow != nullptr)
    {
        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(m_NativeWindow), GLFW_FALSE);
    }
}
