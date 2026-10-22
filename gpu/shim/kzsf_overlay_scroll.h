#pragma once
#include "imgui.h"
#include "imgui_internal.h"

// Register a disabled control for navigation only. Its actual widget still
// runs with Disabled set, so mouse/keyboard/controller activation cannot edit
// it. Keep the original item metadata for disabled hover/help behavior.
inline void KzsfFocusDisabledItem() {
    auto& g=*GImGui;
    const ImGuiLastItemData item=g.LastItemData;
    if(!item.ID || !(item.ItemFlags&ImGuiItemFlags_Disabled))return;
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled,false);
    ImGui::ItemAdd(item.Rect,item.ID,&item.NavRect,ImGuiItemFlags_AllowDuplicateId);
    ImGui::PopItemFlag();
    g.LastItemData=item;
    ImGui::RenderNavCursor(item.NavRect,item.ID);
}

// Keep navigation moving when the focused control reaches the visible edge
// of a tab. Do not steal arrows from an active slider or an open combo.
inline void KzsfOverlayScrollNavigation() {
    auto& g=*GImGui;
    auto* window=ImGui::GetCurrentWindow();
    if(g.NavWindow!=window || g.ActiveId || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return;
    const float row=ImGui::GetFrameHeightWithSpacing();
    float delta=0;
    if(ImGui::IsKeyPressed(ImGuiKey_PageDown))delta=window->InnerRect.GetHeight()*0.8f;
    else if(ImGui::IsKeyPressed(ImGuiKey_PageUp))delta=-window->InnerRect.GetHeight()*0.8f;
    else {
        const ImRect focus=ImGui::WindowRectRelToAbs(window,window->NavRectRel[ImGuiNavLayer_Main]);
        const bool down=ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown);
        const bool up=ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp);
        const bool no_target=g.NavMoveScoringItems && !g.NavMoveResultLocal.ID && !g.NavMoveResultOther.ID;
        if(down && (focus.Max.y+row>=window->InnerRect.Max.y || no_target))delta=row;
        else if(up && (focus.Min.y-row<=window->InnerRect.Min.y || no_target))delta=-row;
    }
    if(delta!=0)ImGui::SetScrollY(ImClamp(window->Scroll.y+delta,0.0f,window->ScrollMax.y));
}
