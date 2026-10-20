#define main mesh_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_generic_pose.h"
static void identity_matrix(float a[16]){memset(a,0,64);for(int i=0;i<4;i++)a[i*5]=1;}
static void rig(int count,size_t skeleton,size_t array,size_t text,size_t binding,size_t names,size_t matrices) {
    integer(skeleton+0x30,count);ptr(skeleton+0x38,origin+array);
    integer(binding+0x10,count);integer(binding+0x28,count);ptr(binding+0x18,origin+names);ptr(binding+0x30,origin+matrices);
    for(int i=0;i<count;i++) {
        char name[32];int length=snprintf(name,sizeof(name),"joint%d",i);size_t string=text+(size_t)i*64;
        integer(string+8,length);memcpy(memory+string+16,name,(size_t)length);ptr(array+(size_t)i*24,origin+string);
        int16_t parent=i==80 ? 70 : (int16_t)(i-1);memcpy(memory+array+(size_t)i*24+16,&parent,2);
        int reversed=count-i-1;ptr(names+(size_t)reversed*8,origin+string);
        float inverse[16];identity_matrix(inverse);inverse[12]=-(float)(i+(count==98 ? 2 : 0));
        memcpy(memory+matrices+(size_t)reversed*64,inverse,64);
    }
}
int main(void) {
    const int counts[]={111,114,125,130,141,153,260,275,KZSF_CHARACTER_GENERIC_MAX};
    for(unsigned test=0;test<sizeof(counts)/sizeof(counts[0]);test++) {
        memset(memory,0,sizeof(memory));int n=counts[test];
        rig(98,0x7000,0x12000,0x16000,0x9000,0xb000,0x24000);
        rig(n,0x8000,0x10000,0x1a000,0xa000,0xc000,0x28000);
        KzsfCharacterMeshLeaf from={.skeleton=origin+0x7000,.bindings=origin+0x9000,.binding_count=98};
        KzsfCharacterMeshLeaf to={.skeleton=origin+0x8000,.bindings=origin+0xa000,.binding_count=n};
        KzsfCharacterGenericPlan forward,back;
        assert(kzsf_character_generic_plan(&from,&to,read_memory,NULL,&forward));
        assert(forward.matched==98 && forward.to_count==n);
        float source[KZSF_CHARACTER_GENERIC_MAX][16],target[KZSF_CHARACTER_GENERIC_MAX][16],restored[KZSF_CHARACTER_GENERIC_MAX][16];
        for(int i=0;i<98;i++){identity_matrix(source[i]);source[i][12]=(float)i+10;}
        source[97][0]=0;source[97][1]=1;source[97][4]=-1;source[97][5]=0;
        assert(kzsf_character_generic_worlds(&forward,source,target));
        for(int i=0;i<n;i++)for(int k=0;k<16;k++)assert(isfinite(target[i][k]));
        assert(fabsf(target[0][12]-8)<1e-4f);
        assert(kzsf_character_generic_plan(&to,&from,read_memory,NULL,&back));
        assert(kzsf_character_generic_worlds(&back,target,restored));
        for(int i=0;i<98;i++)for(int k=0;k<16;k++)assert(fabsf(source[i][k]-restored[i][k])<1e-4f);
        // Native render storage may have an NPC joint layout while matched
        // animated worlds remain in Lucas bind space. Do not reverse an NPC
        // bind correction that the native named copy never applied.
        KzsfCharacterMeshLeaf native_layout=to;native_layout.bindings=from.bindings;native_layout.binding_count=98;
        KzsfCharacterGenericPlan native_to_base;
        assert(kzsf_character_generic_plan(&native_layout,&from,read_memory,NULL,&native_to_base));
        for(int i=0;i<98;i++)memcpy(target[i],source[i],64);
        assert(kzsf_character_generic_worlds(&native_to_base,target,restored));
        for(int i=0;i<98;i++)for(int k=0;k<16;k++)assert(fabsf(source[i][k]-restored[i][k])<1e-4f);
        /* Imported layouts can supply finite but wrong procedural worlds.
         * Rebuild helpers without changing any of the animated joint routes. */
        KzsfCharacterGenericPlan repaired=native_to_base;
        assert(kzsf_character_generic_rebuild_player_helpers(&repaired));
        for(int i=0;i<78;i++)assert(repaired.source[i]==native_to_base.source[i]);
        for(int i=78;i<98;i++)assert(repaired.source[i]==-1);
        for(int i=78;i<98;i++) {
            int source_index=native_to_base.source[i];
            if(source_index>=0)target[source_index][12]=10000.f; // finite corruption
        }
        assert(kzsf_character_generic_worlds(&repaired,target,restored));
        for(int i=0;i<98;i++)for(int k=0;k<16;k++)assert(isfinite(restored[i][k]) && fabsf(restored[i][k])<1000.f);
        repaired.parent[78]=78;assert(!kzsf_character_generic_rebuild_player_helpers(&repaired));
        target[97][0]=NAN;assert(kzsf_character_generic_worlds(&back,target,restored));
        for(int i=0;i<98;i++)for(int k=0;k<16;k++)assert(isfinite(restored[i][k]));
        back.parent[0]=0;assert(!kzsf_character_generic_worlds(&back,target,restored));
        int16_t parent=81;memcpy(memory+0x10000+81*24+16,&parent,2);
        assert(!kzsf_character_generic_plan(&from,&to,read_memory,NULL,&forward));
    }
    puts("generic soldier pose: Chapter3 rigs up to320 joints, inverse round trips, missing joints and parent rejection pass");
}
