#include "../gpu/shim/kzsf_overlay_scroll.h"
#include <cassert>
static float scroll,max_scroll;
static bool disabled_tail=false;
static int focused_row=-1;
static bool values[40]{};
static void frame(bool initial=false) {
 ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,400});ImGui::Begin("Menu");
 ImGui::BeginChild("body",{0,-80},0,ImGuiWindowFlags_AlwaysVerticalScrollbar);
 ImGui::BeginTable("rows",2,ImGuiTableFlags_SizingStretchProp);
 for(int i=0;i<40;i++) {ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("Setting %d",i);ImGui::TableNextColumn();ImGui::PushID(i);ImGui::BeginDisabled(disabled_tail && i>=4);ImGui::Checkbox("##value",&values[i]);KzsfFocusDisabledItem();if(ImGui::IsItemFocused())focused_row=i;if(initial&&i==0)ImGui::SetKeyboardFocusHere(-1);ImGui::EndDisabled();ImGui::PopID();}
 ImGui::EndTable();KzsfOverlayScrollNavigation();auto*w=ImGui::GetCurrentWindow();scroll=w->Scroll.y;max_scroll=w->ScrollMax.y;ImGui::EndChild();ImGui::End();ImGui::Render();
}
static void press(ImGuiKey key,int frames) {for(int i=0;i<frames;i++){ImGui::GetIO().AddKeyEvent(key,i%6<3);frame();}ImGui::GetIO().AddKeyEvent(key,false);frame();}
int main(){ImGui::CreateContext();auto&io=ImGui::GetIO();io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;io.IniFilename=nullptr;unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);
 frame(true);press(ImGuiKey_DownArrow,360);assert(max_scroll>0 && scroll>=max_scroll-1);
 press(ImGuiKey_UpArrow,360);assert(scroll<=1);
 press(ImGuiKey_PageDown,60);assert(scroll>=max_scroll-1);
 press(ImGuiKey_PageUp,60);assert(scroll<=1);
 press(ImGuiKey_GamepadDpadDown,360);assert(scroll>=max_scroll-1);
 disabled_tail=true;frame(true);press(ImGuiKey_UpArrow,360);
 press(ImGuiKey_DownArrow,360);assert(scroll>=max_scroll-1);assert(focused_row==39);press(ImGuiKey_Space,12);assert(!values[39]);
 press(ImGuiKey_UpArrow,360);assert(scroll<=1);
 press(ImGuiKey_GamepadDpadDown,360);assert(scroll>=max_scroll-1);
 ImGui::DestroyContext();}
