#include "../gpu/shim/kzsf_notifications.h"
#include "../gpu/shim/kzsf_overlay_settings_snapshot.h"
#include <cassert>
namespace KzsfSettings { Values& Get(){static Values v;return v;} }
int main(){
 KzsfNotifications q;for(int i=0;i<8;i++)q.Push("Model "+std::to_string(i));
 q.Tick(100,false);assert(q.visible.empty() && q.waiting.size()==8);
 q.Tick(0,true);assert(q.visible.size()==5 && q.waiting.size()==3);
 q.Tick(4,true);q.Tick(100,false);assert(q.visible.front().remaining==1);
 q.Tick(1.1f,true);assert(q.visible.size()==3 && q.waiting.empty());assert(q.visible.front().text=="Model 5");
 q.Tick(5.1f,true);assert(q.Empty());
 auto& s=KzsfSettings::Get();auto before=KzsfOverlaySettingsSnapshot();
 s.fov_percent=120;s.motion_blur=false;s.menu_tab=3;s.game_frame_ms=17;
 auto after=KzsfOverlaySettingsSnapshot();int changes=0;for(auto&[label,value]:after)if(before[label]!=value)changes++;
 assert(changes==2 && after["Field of view"]=="120" && after["Motion blur (restart required)"]=="Off");
 s.fov_percent=100;s.motion_blur=true;assert(KzsfOverlaySettingsSnapshot()==before);
}
