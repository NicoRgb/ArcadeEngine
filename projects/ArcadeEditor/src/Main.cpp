#include "ArcadeEditor/EditorApplication.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        EditorApplication app;
        return app.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "ArcadeEditor failed: " << exception.what() << '\n';
        return 1;
    }
}
