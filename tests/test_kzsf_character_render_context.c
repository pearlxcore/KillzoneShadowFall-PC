#define main mesh_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_render_retarget.h"
static int phase;static KzsfCharacterRenderContext selected;
static void KZSF_CHARACTER_ABI assign(uintptr_t pose,uintptr_t skeleton) {
    assert(skeleton==origin+0xc100);
    assert((phase==0 && pose==selected.current) || (phase==2 && pose==selected.previous));
    ptr((size_t)(pose-origin)+0x20,skeleton);phase++;
}
static void KZSF_CHARACTER_ABI apply(uintptr_t destination,uintptr_t source,const KzsfCharacterNativeMap *map) {
    assert(phase==1 && destination==selected.current && source==selected.source && map->joints.count==103);phase++;
}
static void KZSF_CHARACTER_ABI copy(uintptr_t destination,uintptr_t source,unsigned flags,unsigned extra) {
    assert(phase==3 && destination==selected.previous && source==selected.current && flags==7 && !extra);phase++;
}
int main(void) {
    setup();ptr(0x4018,origin+0x5000);ptr(0x5038,origin);ptr(0x200,origin+0x5000);
    integer(0x5058,2);ptr(0x5060,origin+0x20000);ptr(0x201b8,origin+0x7000);
    ptr(0x200c8,origin+0x6000); /* native default can coexist with Engineer */
    integer(0x4260,0);integer(0x4270,0);integer(0x5260,1);ptr(0x5210,origin+0xc000);
    assert(kzsf_character_render_context(origin+0x4000,origin,origin+0x7000,1,read_memory,NULL,&selected));
    assert(selected.source==origin+0x51f0 && selected.current==origin+0x4190 && selected.previous==origin+0x4200);
    assert(selected.source_skeleton==origin+0xc000);
    unsigned char source_before[0x70];memcpy(source_before,memory+0x51f0,0x70);
    KzsfCharacterNativeMap map={0};map.joints.count=103;
    kzsf_character_render_pair(&selected,origin+0xc100,&map,assign,apply,copy);
    assert(phase==4 && !memcmp(source_before,memory+0x51f0,0x70));
    KzsfCharacterJointMap entries[103],prepared[103];int32_t channels[4],channel_entries[]={0,1,-1,3};
    for(int i=0;i<103;i++)entries[i]=(KzsfCharacterJointMap){i<98 ? i : -1,0,{0}};
    memcpy(memory+0x23000,entries,sizeof(entries));memcpy(memory+0x24000,channel_entries,sizeof(channel_entries));
    map.joints=(KzsfCharacterNativeArray){103,0,origin+0x23000,103,0};
    map.channels=(KzsfCharacterNativeArray){4,0,origin+0x24000,4,0};
    ptr(0x5230,origin+0x25000);ptr(0x5238,origin+0x25000+98*64);
    ptr(0x5240,origin+0x29000);ptr(0x5248,origin+0x29010);
    KzsfCharacterNativeMap view;
    assert(kzsf_character_render_map_prepare(&map,selected.source,read_memory,NULL,prepared,channels,&view));
    assert(prepared[97].parent_differs==1 && prepared[98].source==-1 && !prepared[98].parent_differs);
    assert(view.joints.data==(uintptr_t)prepared && map.joints.data==origin+0x23000);
    entries[1].source=0;memcpy(memory+0x23000,entries,sizeof(entries));
    assert(!kzsf_character_render_map_prepare(&map,selected.source,read_memory,NULL,prepared,channels,&view));
    entries[1].source=1;memcpy(memory+0x23000,entries,sizeof(entries));
    integer(0x24000,4);
    assert(!kzsf_character_render_map_prepare(&map,selected.source,read_memory,NULL,prepared,channels,&view));
    integer(0x24000,0);ptr(0x5238,origin+0x25000+97*64);
    assert(!kzsf_character_render_map_prepare(&map,selected.source,read_memory,NULL,prepared,channels,&view));
    KzsfCharacterRenderContext before=selected;
    assert(!kzsf_character_render_context(origin+0x4000,origin+1,origin+0x7000,1,read_memory,NULL,&selected));
    assert(!memcmp(&before,&selected,sizeof(before)));
    assert(kzsf_character_render_context(origin+0x4000,origin,origin+0x7000,0,read_memory,NULL,&selected));
    ptr(0x200c8,origin+0x7000);ptr(0x201b8,origin+0x6000);
    assert(kzsf_character_render_context(origin+0x4000,origin,origin+0x7000,1,read_memory,NULL,&selected));
    ptr(0x201b8,origin+0x7000);
    assert(kzsf_character_render_context(origin+0x4000,origin,origin+0x7000,1,read_memory,NULL,&selected));
    ptr(0x200c8,origin+0x6000);
    integer(0x4270,2);
    assert(!kzsf_character_render_context(origin+0x4000,origin,origin+0x7000,1,read_memory,NULL,&selected));
    integer(0x4270,0);ptr(0x200,origin+0x6000);
    assert(!kzsf_character_render_context(origin+0x4000,origin,origin+0x7000,1,read_memory,NULL,&selected));
    kzsf_character_render_installed=1;kzsf_character_render_select(origin,origin+0x7000,1);
    assert(kzsf_character_render_owner==origin && kzsf_character_render_slot==1 && !(kzsf_character_render_sequence&1));
    kzsf_character_render_restore(origin,origin+0x5000,origin+0x7000);
    assert(!kzsf_character_render_owner && !kzsf_character_render_part && kzsf_character_render_slot==-1);
    assert(kzsf_character_restore_owner==origin && kzsf_character_restore_model==origin+0x5000 &&
        kzsf_character_restore_part==origin+0x7000 && !(kzsf_character_render_sequence&1));
    kzsf_character_render_select_compatible(origin,origin+0x5000,origin+0x8000,1);
    assert(kzsf_character_render_part==origin+0x8000 && kzsf_character_restore_part==origin+0x7000);
    kzsf_character_render_restore(origin,origin+0x5000,origin+0x8000);
    assert(!kzsf_character_render_owner && kzsf_character_restore_part==origin+0x7000);
    /* The next F7 is a fresh selection, not another restore request. */
    kzsf_character_render_select(origin,origin+0x7000,1);
    assert(kzsf_character_render_owner==origin && !kzsf_character_restore_owner && !kzsf_character_restore_model && !kzsf_character_restore_part);
    kzsf_character_render_restore(origin,0,origin+0x7000);
    assert(!kzsf_character_render_owner && !kzsf_character_restore_owner);
    kzsf_character_render_select(0,0,-1);assert(!kzsf_character_render_owner && kzsf_character_render_slot==-1);
    puts("render retarget context: owner/model/slot isolation, pose buffers, original-source preservation and reset pass");
    return 0;
}
