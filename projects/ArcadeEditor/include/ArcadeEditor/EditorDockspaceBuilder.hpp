#pragma once

#include <imgui.h>

#include <string>
#include <vector>

class EditorDockspaceBuilder
{
public:
    struct Split
    {
        ImGuiDir Direction;
        float Ratio;
        std::string PanelTitle;
    };

    EditorDockspaceBuilder& SplitArea(ImGuiDir direction, float ratio, std::string panelTitle);
    EditorDockspaceBuilder& CenterPanel(std::string panelTitle);

    bool BuildIfNeeded(ImGuiID dockspaceId, const ImGuiViewport& viewport, bool force = false);
    [[nodiscard]] ImGuiID CenterNode() const noexcept { return m_CenterNode; }

private:
    std::vector<Split> m_Splits;
    std::string m_CenterPanelTitle = "Workspace";
    ImGuiID m_CenterNode = 0;
};
