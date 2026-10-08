#include "RuntimeApplication.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

int main(int argc, char** argv)
{
    std::filesystem::path assetDirectory;
    if (argc == 3 && std::string_view(argv[1]) == "--assets")
    {
        assetDirectory = argv[2];
    }
    else if (argc != 1)
    {
        std::cerr << "Usage: ArcadeRuntime [--assets <directory>]\n";
        return 2;
    }

    try
    {
        RuntimeApplication app(std::move(assetDirectory));
        return app.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "ArcadeRuntime failed: " << exception.what() << '\n';
        return 1;
    }
}
