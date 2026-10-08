#pragma once

#include <filesystem>

class RuntimeApplication
{
public:
    explicit RuntimeApplication(std::filesystem::path assetDirectory = {});

    int Run();

private:
    std::filesystem::path m_AssetDirectory;
};
