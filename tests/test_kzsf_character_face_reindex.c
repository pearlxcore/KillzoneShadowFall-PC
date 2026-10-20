#define main resource_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_face.h"
static void skeleton_names(size_t skeleton,size_t array,size_t text,int count,int shift) {
    integer(skeleton+0x30,count);ptr(skeleton+0x38,origin+array);
    for(int i=0;i<count;i++) {
        char name[32];int n=snprintf(name,sizeof(name),"bone%d",(i+shift)%count);
        size_t string=text+(size_t)i*64;
        integer(string+8,n);memcpy(memory+string+16,name,(size_t)n);ptr(array+(size_t)i*24,origin+string);
    }
}
int main(void) {
    const int counts[]={98,103,111,130,260,275};
    for(unsigned test=0;test<sizeof(counts)/sizeof(counts[0]);test++) {
        memset(memory,0,sizeof(memory));int count=counts[test];
        // The face map uses animation78; neither render layout needs a mesh
        // with that skeleton or a binding table for the animation skeleton.
        skeleton_names(0x7000,0x10000,0x18000,78,0);
        skeleton_names(0x8000,0x13000,0x20000,count,17);
        memset(memory+0x2c000,0xa5,0xf0);
        ptr(0x2c060,origin+0x7000);integer(0x2c0b8,143);ptr(0x2c0c0,origin+0x2d000);
        integer(0x2c0d0,6);ptr(0x2c0d8,origin+0x2e000);
        for(int i=0;i<143;i++)integer(0x2d000+(size_t)i*4,i<3 ? 7+i : -1);
        unsigned char original[0xf0],prepared[0xf0];int32_t mapped[512];
        memcpy(original,memory+0x2c000,sizeof(original));
        assert(kzsf_character_face_reindex(origin+0x2c000,origin+0x8000,read_memory,NULL,prepared,mapped));
        for(int i=0;i<3;i++)assert(mapped[i]==(7+i+count-17)%count);
        for(int i=3;i<143;i++)assert(mapped[i]==-1);
        assert(!memcmp(original,memory+0x2c000,sizeof(original))); // shared variant untouched
        uintptr_t actual=origin+0x8000,address=(uintptr_t)mapped;
        memcpy(original+0x60,&actual,8);memcpy(original+0xc0,&address,8);
        assert(!memcmp(prepared,original,sizeof(original))); // native offset pointers/counts preserved
        integer(0x2d000,78); // out of range in animation78, even though valid in render98+
        assert(!kzsf_character_face_reindex(origin+0x2c000,actual,read_memory,NULL,prepared,mapped));
        integer(0x2d000,-2);
        assert(!kzsf_character_face_reindex(origin+0x2c000,actual,read_memory,NULL,prepared,mapped));
        integer(0x2d000,7);integer(0x2c0b8,513);
        assert(!kzsf_character_face_reindex(origin+0x2c000,actual,read_memory,NULL,prepared,mapped));
    }
    puts("facial reindex: animation78 -> reordered render98..275, native offsets preserved, bounds pass");
}
