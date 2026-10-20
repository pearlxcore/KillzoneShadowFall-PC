#define main resource_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_face.h"
static int creates,attaches,detaches,destroys,dirties;
static uintptr_t rep=0x10016000,owner=0x10015000,resource=0x10017000,face=0x10018000;
static int write_memory(void *opaque,uintptr_t address,const void *data,size_t count) {
    (void)opaque;
    if(address<origin || address-origin>sizeof(memory) || count>sizeof(memory)-(address-origin))return 0;
    memcpy(memory+address-origin,data,count);return 1;
}
static uintptr_t KZSF_CHARACTER_ABI create_face(uintptr_t res,uintptr_t parent) {
    assert(res==resource && parent==rep);creates++;
    ptr(0x18000,origin+0x3cdf670);ptr(0x18018,resource);return face;
}
static void KZSF_CHARACTER_ABI attach_face(uintptr_t parent,uintptr_t value) {
    uintptr_t current=0;assert(parent==rep+8 && value==face);
    assert(read_memory(NULL,rep+0x600,&current,8) && current==face);attaches++;
}
static void KZSF_CHARACTER_ABI detach_face(uintptr_t parent,uintptr_t value) {
    assert(parent==rep+8 && value==face);detaches++;
}
static void KZSF_CHARACTER_ABI destroy_face(uintptr_t value) {
    uintptr_t current=1;assert(value==face);
    assert(read_memory(NULL,rep+0x600,&current,8) && !current);destroys++;
}
static void KZSF_CHARACTER_ABI dirty_face(uintptr_t parent) {
    assert(parent==rep+0x40);assert(memory[0x16190]&0x10);assert(memory[0x161e1]&8);dirties++;
}
int main(void) {
    setup();ptr(0x15000,origin+0x3ca98b0);ptr(0x151d8,rep);ptr(0x151d0,origin+0x1000);
    ptr(0x16000,origin+0x3caab30);ptr(0x16198,owner);
    ptr(0x1000,origin+0x3caac10);ptr(0x16f0,origin+0x19000);ptr(0x19000,origin+0x3cdfa40);
    integer(0x1648,1);ptr(0x1650,origin+0x20000);ptr(0x20008,origin+0x7000);ptr(0x20018,resource);
    ptr(0x17000,origin+0x3cdfb20);ptr(0x17030,origin+0x9000);ptr(0x17038,origin+0xc000);
    leaf(0x9000,0xb000);ptr(0xc000,origin+0x3c1ec70);integer(0xc030,143);
    ptr(0xc800,origin+0x3c1ec70);integer(0xc830,98);ptr(0x20060,origin+0xc800);
    integer(0x200b8,143);ptr(0x200c0,origin+0x21000);integer(0x200d0,1);ptr(0x200d8,origin+0x22000);
    for(int i=0;i<143;i++)integer(0x21000+(size_t)i*4,i ? -1 : 7);
    for(int i=0;i<16;i++){float value=(float)(i%5==0);memcpy(memory+0x22000+(size_t)i*4,&value,4);}
    uintptr_t found=0;
    assert(kzsf_character_face_resource(origin+0x1000,origin+0x7000,origin,read_memory,NULL,type_of,&found) && found==resource);
    /* Special Forces/Infantry share one body across distinct face variants.
     * An explicit selected variant resolves that ambiguity without weakening
     * validation of its own face map or accepting an arbitrary other body. */
    integer(0x1648,2);memcpy(memory+0x200f0,memory+0x20000,0xf0);
    memcpy(memory+0x17500,memory+0x17000,0x40);ptr(0x20108,origin+0x17500);
    assert(!kzsf_character_face_resource(origin+0x1000,origin+0x7000,origin,read_memory,NULL,type_of,&found));
    assert(kzsf_character_face_resource_variant(origin+0x1000,origin+0x7000,0,origin,read_memory,NULL,type_of,&found) && found==resource);
    assert(kzsf_character_face_resource_variant(origin+0x1000,origin+0x7000,1,origin,read_memory,NULL,type_of,&found) && found==origin+0x17500);
    assert(!kzsf_character_face_resource_variant(origin+0x1000,origin+0x7000,2,origin,read_memory,NULL,type_of,&found));
    assert(!kzsf_character_face_resource_variant(origin+0x1000,origin+0x7100,0,origin,read_memory,NULL,type_of,&found));
    ptr(0x201b0,0); /* variant1 map at c0 */
    assert(!kzsf_character_face_resource_variant(origin+0x1000,origin+0x7000,1,origin,read_memory,NULL,type_of,&found));
    assert(kzsf_character_face_resource_variant(origin+0x1000,origin+0x7000,0,origin,read_memory,NULL,type_of,&found));
    integer(0x1648,1);
    /* Regression: original player's missing face map caused e46aa3 null dereference. */
    ptr(0x200c0,0);
    assert(!kzsf_character_face_resource(origin+0x1000,origin+0x7000,origin,read_memory,NULL,type_of,&found));
    ptr(0x200c0,origin+0x21000);integer(0x200b8,142);
    assert(!kzsf_character_face_mapping(origin+0x20000,resource,origin,read_memory,NULL,type_of));
    integer(0x200b8,143);integer(0x21000,98);
    assert(!kzsf_character_face_mapping(origin+0x20000,resource,origin,read_memory,NULL,type_of));
    integer(0x21000,7);integer(0x200d0,0);
    assert(!kzsf_character_face_mapping(origin+0x20000,resource,origin,read_memory,NULL,type_of));
    integer(0x200d0,1);
    /* Live baseline: neckRoot, neck and chest map to three body bones,
     * while the prepared correction array contains six matrices. */
    integer(0x21004,8);integer(0x21000+140*4,9);integer(0x200d0,6);
    for(int i=1;i<6;i++)memcpy(memory+0x22000+(size_t)i*64,memory+0x22000,64);
    assert(kzsf_character_face_mapping(origin+0x20000,resource,origin,read_memory,NULL,type_of));
    integer(0x200d0,2);
    assert(!kzsf_character_face_mapping(origin+0x20000,resource,origin,read_memory,NULL,type_of));
    integer(0x200d0,6);
    unsigned char patch[55];uintptr_t callback=0x12345678,encoded=0;int32_t skip=0;
    assert(kzsf_character_face_mapping_patch(patch,callback));memcpy(&encoded,patch+11,8);memcpy(&skip,patch+32,4);
    assert(encoded==callback && 0xe46a4d+36+skip==0xe46d7c);
    assert(patch[0]==0x41 && patch[1]==0x53 && patch[25]==0x41 && patch[26]==0x5b);
    assert(!kzsf_character_face_mapping_patch(patch,0));
    unsigned char stub[104],offset_patch[47];uintptr_t stub_address=origin+0x100000;
    assert(kzsf_character_face_offsets_bridge(stub,sizeof(stub),stub_address,offset_patch,origin,callback));
    memcpy(&encoded,stub+39,8);assert(encoded==callback);
    memcpy(&skip,stub+88,4);assert(stub_address+92+skip==origin+0xe4680c);
    memcpy(&skip,stub+100,4);assert(stub_address+104+skip==origin+0xe465fb);
    memcpy(&skip,offset_patch+1,4);assert(origin+0xe465cc+5+skip==stub_address);
    assert(!kzsf_character_face_offsets_bridge(stub,103,stub_address,offset_patch,origin,callback));
    /* A face mesh on a different skeleton must not be attached. */
    ptr(0x9058,origin+0xc100);ptr(0xc100,origin+0x3c1ec70);
    assert(!kzsf_character_face_resource(origin+0x1000,origin+0x7000,origin,read_memory,NULL,type_of,&found));
    ptr(0x9058,origin+0xc000);
    KzsfCharacterFace state={0};KzsfCharacterFaceApi api={create_face,attach_face,detach_face,destroy_face,dirty_face,write_memory};
    /* Ownership, occupied native face, and missing fallback all refuse before allocation. */
    ptr(0x16198,owner+8);
    assert(!kzsf_character_face_enable(&state,owner,resource,origin,read_memory,NULL,type_of,api));ptr(0x16198,owner);
    ptr(0x16600,origin+0x21000);
    assert(!kzsf_character_face_enable(&state,owner,resource,origin,read_memory,NULL,type_of,api));ptr(0x16600,0);
    ptr(0x16f0,0);
    assert(!kzsf_character_face_enable(&state,owner,resource,origin,read_memory,NULL,type_of,api));ptr(0x16f0,origin+0x19000);
    assert(!creates);
    assert(kzsf_character_face_enable(&state,owner,resource,origin,read_memory,NULL,type_of,api));
    assert(creates==1 && attaches==1 && dirties==1 && state.face==face);
    assert(kzsf_character_face_enable(&state,owner,resource,origin,read_memory,NULL,type_of,api));
    assert(creates==1 && attaches==1);
    assert(kzsf_character_face_disable(&state,owner,origin,read_memory,NULL,type_of,api));
    assert(detaches==1 && destroys==1 && dirties==2 && !state.face);
    assert(kzsf_character_face_disable(&state,owner,origin,read_memory,NULL,type_of,api));assert(destroys==1);
    /* A native rebuild may replace/remove the animator; never free the replacement. */
    state=(KzsfCharacterFace){owner,rep,face,resource};ptr(0x16600,origin+0x21000);
    assert(kzsf_character_face_disable(&state,owner,origin,read_memory,NULL,type_of,api));
    assert(!state.face && destroys==1 && detaches==1);
    puts("character facial companion checks passed");return 0;
}
