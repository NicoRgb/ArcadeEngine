#pragma once

#include "Core/Result.hpp"

#include <filesystem>

enum class EditorTheme
{
    ArcadeGraphite,
    Light,
    HighContrast
};

class EditorThemeManager
{
public:
    explicit EditorThemeManager(std::filesystem::path settingsFile);
    [[nodiscard]] EditorTheme Current() const noexcept { return m_Current; }
    [[nodiscard]] Result<void> Load();
    [[nodiscard]] Result<void> Set(EditorTheme theme);
    void Apply() const;

private:
    std::filesystem::path m_SettingsFile;
    EditorTheme m_Current = EditorTheme::ArcadeGraphite;
};
