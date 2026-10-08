#include "ArcadeEditor/EditorTheme.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
using json = nlohmann::json;

const char* ThemeName(EditorTheme theme)
{
    switch (theme)
    {
    case EditorTheme::ArcadeGraphite:
        return "ArcadeGraphite";
    case EditorTheme::Light:
        return "Light";
    case EditorTheme::HighContrast:
        return "HighContrast";
    }
    return "ArcadeGraphite";
}

ImVec4 Color(unsigned int rgb, float alpha = 1.0F)
{
    return ImVec4(float((rgb >> 16U) & 0xffU) / 255.0F, float((rgb >> 8U) & 0xffU) / 255.0F,
                  float(rgb & 0xffU) / 255.0F, alpha);
}

void ApplyBaseStyle()
{
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 7.0F;
    style.ChildRounding = 5.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 5.0F;
    style.ScrollbarRounding = 7.0F;
    style.GrabRounding = 4.0F;
    style.TabRounding = 4.0F;
    style.WindowBorderSize = 0.0F;
    style.FrameBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    style.WindowPadding = ImVec2(11.0F, 9.0F);
    style.FramePadding = ImVec2(8.0F, 5.0F);
    style.ItemSpacing = ImVec2(7.0F, 6.0F);
    style.ScrollbarSize = 12.0F;
}

void ApplyGraphite()
{
    ImGui::StyleColorsDark();
    ApplyBaseStyle();
    auto* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text] = Color(0xdce5f3);
    c[ImGuiCol_TextDisabled] = Color(0x8290a5);
    c[ImGuiCol_WindowBg] = Color(0x171c24);
    c[ImGuiCol_ChildBg] = Color(0x141920);
    c[ImGuiCol_PopupBg] = Color(0x202732);
    c[ImGuiCol_Border] = Color(0x303a48);
    c[ImGuiCol_FrameBg] = Color(0x222b37);
    c[ImGuiCol_FrameBgHovered] = Color(0x2f3c4d);
    c[ImGuiCol_FrameBgActive] = Color(0x394a60);
    c[ImGuiCol_TitleBg] = Color(0x141920);
    c[ImGuiCol_TitleBgActive] = Color(0x202a38);
    c[ImGuiCol_MenuBarBg] = Color(0x141920);
    c[ImGuiCol_Button] = Color(0x293545);
    c[ImGuiCol_ButtonHovered] = Color(0x384b62);
    c[ImGuiCol_ButtonActive] = Color(0x4b5f7c);
    c[ImGuiCol_Header] = Color(0x29374a);
    c[ImGuiCol_HeaderHovered] = Color(0x354a64);
    c[ImGuiCol_HeaderActive] = Color(0x425c7b);
    c[ImGuiCol_Separator] = Color(0x303a48);
    c[ImGuiCol_Tab] = Color(0x202732);
    c[ImGuiCol_TabHovered] = Color(0x455876);
    c[ImGuiCol_TabSelected] = Color(0x303f54);
    c[ImGuiCol_DockingPreview] = Color(0x9a84ff, 0.65F);
    c[ImGuiCol_DockingEmptyBg] = Color(0x11161c);
    c[ImGuiCol_TextSelectedBg] = Color(0x675caf, 0.55F);
    c[ImGuiCol_NavCursor] = Color(0xa996ff);
}

void ApplyLight()
{
    ImGui::StyleColorsLight();
    ApplyBaseStyle();
    auto* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_WindowBg] = Color(0xe9edf2);
    c[ImGuiCol_ChildBg] = Color(0xf2f4f7);
    c[ImGuiCol_PopupBg] = Color(0xffffff);
    c[ImGuiCol_Border] = Color(0xc4ccd5);
    c[ImGuiCol_FrameBg] = Color(0xdce2e9);
    c[ImGuiCol_FrameBgHovered] = Color(0xcbd7e4);
    c[ImGuiCol_FrameBgActive] = Color(0xb9cce0);
    c[ImGuiCol_TitleBg] = Color(0xdce2e9);
    c[ImGuiCol_TitleBgActive] = Color(0xcbd7e4);
    c[ImGuiCol_MenuBarBg] = Color(0xdce2e9);
    c[ImGuiCol_Button] = Color(0xd2dce7);
    c[ImGuiCol_ButtonHovered] = Color(0xbccde0);
    c[ImGuiCol_ButtonActive] = Color(0xa9c0d9);
    c[ImGuiCol_Header] = Color(0xcbd8e7);
    c[ImGuiCol_HeaderHovered] = Color(0xb9cee3);
    c[ImGuiCol_HeaderActive] = Color(0xa4bfdc);
    c[ImGuiCol_Tab] = Color(0xd3dce6);
    c[ImGuiCol_TabSelected] = Color(0xffffff);
    c[ImGuiCol_DockingPreview] = Color(0x755bd6, 0.55F);
    c[ImGuiCol_TextSelectedBg] = Color(0x8671d7, 0.35F);
}

void ApplyHighContrast()
{
    ImGui::StyleColorsDark();
    ApplyBaseStyle();
    auto* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text] = Color(0xffffff);
    c[ImGuiCol_TextDisabled] = Color(0xc2c2c2);
    c[ImGuiCol_WindowBg] = Color(0x090909);
    c[ImGuiCol_ChildBg] = Color(0x101010);
    c[ImGuiCol_PopupBg] = Color(0x111111);
    c[ImGuiCol_Border] = Color(0x858585);
    c[ImGuiCol_FrameBg] = Color(0x202020);
    c[ImGuiCol_FrameBgHovered] = Color(0x343434);
    c[ImGuiCol_FrameBgActive] = Color(0x454545);
    c[ImGuiCol_Button] = Color(0x262626);
    c[ImGuiCol_ButtonHovered] = Color(0x424242);
    c[ImGuiCol_ButtonActive] = Color(0x5a5a5a);
    c[ImGuiCol_Header] = Color(0x292929);
    c[ImGuiCol_HeaderHovered] = Color(0x444444);
    c[ImGuiCol_HeaderActive] = Color(0x595959);
    c[ImGuiCol_Tab] = Color(0x202020);
    c[ImGuiCol_TabSelected] = Color(0x414141);
    c[ImGuiCol_DockingPreview] = Color(0xffd500, 0.7F);
    c[ImGuiCol_NavCursor] = Color(0xffdf00);
}
} // namespace

EditorThemeManager::EditorThemeManager(std::filesystem::path settingsFile)
    : m_SettingsFile(std::move(settingsFile))
{
}

Result<void> EditorThemeManager::Load()
{
    std::error_code error;
    if (!std::filesystem::exists(m_SettingsFile, error))
    {
        return error ? Result<void>(MAKE_ERROR_EXT(Error::IoFailure,
                                                   "Unable to inspect editor settings.", error))
                     : Result<void>{};
    }
    std::ifstream input(m_SettingsFile);
    if (!input)
    {
        return MAKE_ERROR_MSG(Error::PermissionDenied, "Unable to read editor settings.");
    }
    try
    {
        const auto data = json::parse(input);
        if (data.contains("theme") && data["theme"].is_string())
        {
            const auto name = data["theme"].get<std::string>();
            if (name == "Light")
                m_Current = EditorTheme::Light;
            else if (name == "HighContrast")
                m_Current = EditorTheme::HighContrast;
            else
                m_Current = EditorTheme::ArcadeGraphite;
        }
    }
    catch (const json::exception& exception)
    {
        return MAKE_ERROR_MSG(Error::ParseFailure, exception.what());
    }
    return {};
}

Result<void> EditorThemeManager::Set(EditorTheme theme)
{
    std::error_code error;
    if (!m_SettingsFile.parent_path().empty())
    {
        std::filesystem::create_directories(m_SettingsFile.parent_path(), error);
        if (error)
        {
            return MAKE_ERROR_EXT(Error::IoFailure, "Unable to create editor settings directory.",
                                  error);
        }
    }
    auto temporary = m_SettingsFile;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return MAKE_ERROR_MSG(Error::PermissionDenied, "Unable to save editor settings.");
    }
    output << json{{"theme", ThemeName(theme)}}.dump(2) << '\n';
    output.flush();
    if (!output)
    {
        output.close();
        std::filesystem::remove(temporary, error);
        return MAKE_ERROR_MSG(Error::IoFailure, "Unable to save editor settings.");
    }
    output.close();
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), m_SettingsFile.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto systemError =
            std::error_code(static_cast<int>(GetLastError()), std::system_category());
        std::filesystem::remove(temporary, error);
        return MAKE_ERROR_EXT(Error::IoFailure, "Unable to replace editor settings.", systemError);
    }
#else
    std::filesystem::rename(temporary, m_SettingsFile, error);
    if (error)
    {
        const auto replaceError = error;
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return MAKE_ERROR_EXT(Error::IoFailure, "Unable to replace editor settings.", replaceError);
    }
#endif
    m_Current = theme;
    Apply();
    return {};
}

void EditorThemeManager::Apply() const
{
    switch (m_Current)
    {
    case EditorTheme::ArcadeGraphite:
        ApplyGraphite();
        break;
    case EditorTheme::Light:
        ApplyLight();
        break;
    case EditorTheme::HighContrast:
        ApplyHighContrast();
        break;
    }
}
