#include "ArcadeEditor/EditorDockspaceBuilder.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <utility>

EditorDockspaceBuilder& EditorDockspaceBuilder::SplitArea(ImGuiDir direction, float ratio,
                                                          std::string panelTitle)
{
    m_Splits.push_back({direction, std::clamp(ratio, 0.05F, 0.9F), std::move(panelTitle)});
    return *this;
}

EditorDockspaceBuilder& EditorDockspaceBuilder::CenterPanel(std::string panelTitle)
{
    m_CenterPanelTitle = std::move(panelTitle);
    return *this;
}

bool EditorDockspaceBuilder::BuildIfNeeded(ImGuiID dockspaceId, const ImGuiViewport& viewport,
                                           bool force)
{
    if (!force && ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
    {
        if (const ImGuiDockNode* center = ImGui::DockBuilderGetCentralNode(dockspaceId))
            m_CenterNode = center->ID;
        return false;
    }

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport.WorkSize);

    ImGuiID remaining = dockspaceId;
    for (const Split& split : m_Splits)
    {
        ImGuiID panelNode = 0;
        ImGuiID remainingNode = 0;
        ImGui::DockBuilderSplitNode(remaining, split.Direction, split.Ratio, &panelNode,
                                    &remainingNode);
        ImGui::DockBuilderDockWindow(split.PanelTitle.c_str(), panelNode);
        remaining = remainingNode;
    }

    ImGui::DockBuilderDockWindow(m_CenterPanelTitle.c_str(), remaining);
    ImGui::DockBuilderFinish(dockspaceId);
    const ImGuiDockNode* center = ImGui::DockBuilderGetCentralNode(dockspaceId);
    m_CenterNode = center != nullptr ? center->ID : remaining;
    return true;
}
