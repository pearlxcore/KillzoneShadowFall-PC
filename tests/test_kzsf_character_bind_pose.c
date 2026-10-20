#define main mesh_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_bind_pose.h"
static void identity(float m[16]){memset(m,0,64);for(int i=0;i<4;i++)m[i*5]=1;}
static int write_memory(void *opaque,uintptr_t p,const void *data,size_t n) {
    (void)opaque;if(p<origin || p-origin>sizeof(memory) || n>sizeof(memory)-(p-origin))return 0;
    memcpy(memory+p-origin,data,n);return 1;
}
int main(void) {
    setup();integer(0xc030,103);ptr(0xc038,origin+0x10000);
    ptr(0xc800,origin+0x3c1ec70);integer(0xc830,98);ptr(0xc838,origin+0x16000);
    integer(0xb010,103);integer(0xb028,103);ptr(0xb018,origin+0x12000);ptr(0xb030,origin+0x18000);
    integer(0xb810,98);ptr(0xb818,origin+0x1a000);integer(0xb828,98);ptr(0xb830,origin+0x1d000);
    KzsfCharacterMeshLeaf source={.skeleton=origin+0xc800,.bindings=origin+0xb800,.binding_count=98};
    KzsfCharacterMeshLeaf target={.skeleton=origin+0xc000,.bindings=origin+0xb000,.binding_count=103};
    KzsfCharacterJointMap joints[103];float inverse[16];
    for(int i=0;i<103;i++) {
        char name[32];int n=snprintf(name,sizeof(name),"joint%d",i);size_t target_text=0x13000+(size_t)i*64;
        integer(target_text+8,n);memcpy(memory+target_text+0x10,name,(size_t)n);
        ptr(0x10000+(size_t)i*0x18,origin+target_text);
        int16_t target_parent=(int16_t)(i-1);memcpy(memory+0x10010+(size_t)i*0x18,&target_parent,2);
        ptr(0x12000+(size_t)(102-i)*8,origin+target_text);
        identity(inverse);inverse[12]=-(float)i;memcpy(memory+0x18000+(size_t)(102-i)*64,inverse,64);
        if(i<98) {
            size_t source_text=0x27000+(size_t)i*64;
            integer(source_text+8,n);memcpy(memory+source_text+0x10,name,(size_t)n);
            ptr(0x16000+(size_t)i*0x18,origin+source_text);
            int16_t source_parent=(int16_t)(i-1);memcpy(memory+0x16010+(size_t)i*0x18,&source_parent,2);
            ptr(0x1a000+(size_t)i*8,origin+source_text);
            identity(inverse);inverse[12]=-(float)(i+2);memcpy(memory+0x1d000+(size_t)i*64,inverse,64);
        }
        joints[i]=(KzsfCharacterJointMap){.source=i<98 ? i : -1};
    }
    KzsfCharacterBindPlan plan,before;
    assert(kzsf_character_bind_plan(&source,&target,joints,read_memory,NULL,&plan));
    assert(plan.missing[98] && plan.parent[98]==97 && fabsf(plan.local[98][12]-1)<1e-5f);
    float source_world[98][16],worlds[103][16];
    for(int i=0;i<98;i++){identity(source_world[i]);source_world[i][12]=10+(float)i;}
    source_world[97][0]=0;source_world[97][1]=1;source_world[97][4]=-1;source_world[97][5]=0;
    assert(kzsf_character_bind_worlds(&plan,source_world,worlds));
    assert(fabsf(worlds[0][12]-8)<1e-5f && fabsf(worlds[97][12]-107)<1e-5f);
    assert(fabsf(worlds[98][12]-107)<1e-5f && fabsf(worlds[98][13]+1)<1e-5f);
    assert(fabsf(worlds[102][13]-3)<1e-5f);
    KzsfCharacterReverseBindPlan reverse;float recovered[98][16];
    assert(kzsf_character_reverse_bind_plan(&plan,&reverse));
    assert(kzsf_character_reverse_bind_worlds(&reverse,worlds,recovered));
    for(int i=0;i<98;i++)for(int k=0;k<16;k++)assert(fabsf(recovered[i][k]-source_world[i][k])<1e-4f);
    KzsfCharacterBindPlan invalid=plan;invalid.source[1]=invalid.source[0];
    assert(!kzsf_character_reverse_bind_plan(&invalid,&reverse));
    invalid=plan;memset(invalid.correction[0],0,64);
    assert(!kzsf_character_reverse_bind_plan(&invalid,&reverse));
    assert(kzsf_character_reverse_bind_plan(&plan,&reverse));reverse.target[0]=103;
    assert(!kzsf_character_reverse_bind_worlds(&reverse,worlds,recovered));
    ptr(0x2040,origin+0x21000);ptr(0x2048,origin+0x21000+sizeof(worlds));
    ptr(0x2140,origin+0x23000);ptr(0x2148,origin+0x23000+sizeof(worlds));
    ptr(0x3040,origin+0x25000);ptr(0x3048,origin+0x25000+sizeof(source_world));
    memcpy(memory+0x25000,source_world,sizeof(source_world));
    unsigned char snapshot[0x20000];memcpy(snapshot,memory,sizeof(snapshot));
    assert(kzsf_character_bind_pose(&plan,origin+0x3000,origin+0x2000,origin+0x2100,read_memory,write_memory,NULL));
    assert(!memcmp(memory+0x21000,memory+0x23000,sizeof(worlds)));
    assert(!memcmp(snapshot,memory,sizeof(snapshot)));
    ptr(0x2148,origin+0x23000+64);
    assert(!kzsf_character_bind_pose(&plan,origin+0x3000,origin+0x2000,origin+0x2100,read_memory,write_memory,NULL));
    assert(kzsf_character_reverse_bind_plan(&plan,&reverse));
    ptr(0x4040,origin+0x5000);ptr(0x4048,origin+0x5000+sizeof(worlds));
    ptr(0x4050,origin+0x7000);ptr(0x4058,origin+0x7010);
    memcpy(memory+0x5000,worlds,sizeof(worlds));
    assert(kzsf_character_reverse_bind_pose(&reverse,origin+0x4000,origin+0x2000,read_memory,write_memory,NULL));
    for(int i=0;i<98;i++)for(int k=0;k<16;k++) {
        float value;memcpy(&value,memory+0x21000+(size_t)i*64+(size_t)k*4,4);
        assert(fabsf(value-source_world[i][k])<1e-4f);
    }
    assert(!memcmp(memory+0x5000,worlds,sizeof(worlds)));
    ptr(0x2048,origin+0x21000+64);
    assert(!kzsf_character_reverse_bind_pose(&reverse,origin+0x4000,origin+0x2000,read_memory,write_memory,NULL));
    KzsfCharacterJointMap reverse_joints[98];int32_t channels[4];KzsfCharacterNativeMap view;
    for(int i=0;i<98;i++){KzsfCharacterJointMap joint={.source=i};memcpy(memory+0x8000+(size_t)i*8,&joint,8);}
    for(int i=0;i<4;i++)integer(0x9000+(size_t)i*4,i);
    KzsfCharacterNativeMap reverse_map={.joints={.count=98,.capacity=98,.data=origin+0x8000},
        .channels={.count=4,.capacity=4,.data=origin+0x9000}};
    assert(kzsf_character_reverse_map_prepare(&reverse_map,origin+0x4000,read_memory,NULL,reverse_joints,channels,&view));
    assert(reverse_joints[97].parent_differs==1 && channels[3]==3);
    integer(0x8008,0);
    assert(!kzsf_character_reverse_map_prepare(&reverse_map,origin+0x4000,read_memory,NULL,reverse_joints,channels,&view));
    integer(0x8008,1);integer(0x9000,4);
    assert(!kzsf_character_reverse_map_prepare(&reverse_map,origin+0x4000,read_memory,NULL,reverse_joints,channels,&view));
    integer(0x9000,0);ptr(0x4048,origin+0x5000+98*64);
    assert(!kzsf_character_reverse_map_prepare(&reverse_map,origin+0x4000,read_memory,NULL,reverse_joints,channels,&view));
    float broken[103][16],repaired_worlds[103][16];int repaired_count=-1;
    memcpy(broken,worlds,sizeof(broken));
    for(int i=98;i<103;i++)broken[i][0]=NAN;
    assert(kzsf_character_repair103_worlds(&plan,broken,repaired_worlds,&repaired_count) && repaired_count==5);
    assert(!memcmp(repaired_worlds,worlds,98*64));
    for(int i=0;i<103;i++)for(int k=0;k<16;k++)assert(isfinite(repaired_worlds[i][k]));
    broken[97][0]=NAN;
    assert(!kzsf_character_repair103_worlds(&plan,broken,repaired_worlds,&repaired_count));
    before=plan;int16_t bad=98;memcpy(memory+0x10010+98*0x18,&bad,2);
    assert(!kzsf_character_bind_plan(&source,&target,joints,read_memory,NULL,&plan));assert(!memcmp(&before,&plan,sizeof(plan)));
    bad=97;memcpy(memory+0x10010+98*0x18,&bad,2);
    memset(memory+0x18000+4*64,0,64);assert(!kzsf_character_bind_plan(&source,&target,joints,read_memory,NULL,&plan));
    identity(inverse);inverse[0]=NAN;memcpy(memory+0x18000+4*64,inverse,64);
    assert(!kzsf_character_bind_plan(&source,&target,joints,read_memory,NULL,&plan));
    float a[16],b[16],c[16];identity(a);a[0]=2;a[5]=3;a[12]=7;a[13]=-2;
    assert(kzsf_character_matrix_inverse(a,b) && kzsf_character_matrix_multiply(a,b,c));
    for(int i=0;i<16;i++)assert(fabsf(c[i]-(float)(i%5==0))<1e-5f);
    puts("bind pose: source/target rest correction, reordered names, chained offsets, source preservation, singular/nonfinite/cycle rejection pass");
    return 0;
}
