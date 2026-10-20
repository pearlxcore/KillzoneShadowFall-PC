#define main resource_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_face_bind.h"
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
    const int counts[]={111,114,125,130,260,275,320};
    for(unsigned test=0;test<sizeof(counts)/sizeof(counts[0]);test++) {
        memset(memory,0,sizeof(memory));int n=counts[test];
        rig(98,0x7000,0x12000,0x16000,0x9000,0xb000,0x24000);
        rig(n,0x8000,0x10000,0x1a000,0xa000,0xc000,0x28000);
        integer(0x6030,78);ptr(0x6038,origin+0xe000);memcpy(memory+0xe000,memory+0x12000,78*24);
        ptr(0x2e060,origin+0x6000);integer(0x2e0b8,5);ptr(0x2e0c0,origin+0x2f000);
        integer(0x2e0d0,6);ptr(0x2e0d8,origin+0xf000);
        const int32_t indices[]={-1,7,8,-1,9};memcpy(memory+0x2f000,indices,sizeof(indices));
        float native[6][16];for(int i=0;i<6;i++){identity_matrix(native[i]);native[i][14]=(float)i*.02f;}
        memcpy(memory+0xf000,native,sizeof(native));
        KzsfCharacterMeshLeaf original={.skeleton=origin+0x7000,.bindings=origin+0x9000,.binding_count=98};
        KzsfCharacterMeshLeaf target={.skeleton=origin+0x8000,.bindings=origin+0xa000,.binding_count=n};
        unsigned char prepared[0xf0];int32_t mapped[512];float corrected[512][16];KzsfCharacterGenericPlan plan;
        assert(kzsf_character_face_bind_variant(origin+0x2e000,original.skeleton,&original,&target,
            read_memory,NULL,prepared,mapped,corrected,&plan));
        float source[KZSF_CHARACTER_GENERIC_MAX][16],body[KZSF_CHARACTER_GENERIC_MAX][16];
        for(int i=0;i<98;i++){identity_matrix(source[i]);source[i][12]=(float)i*.1f;}
        source[8][0]=0;source[8][1]=1;source[8][4]=-1;source[8][5]=0;
        assert(kzsf_character_generic_worlds(&plan,source,body));
        int cursor=0;
        for(int i=0;i<5;i++)if(indices[i]>=0) {
            float expected[16],actual[16];
            assert(kzsf_character_matrix_multiply(body[indices[i]],native[cursor],expected));
            assert(kzsf_character_matrix_multiply(source[mapped[i]],corrected[cursor],actual));
            for(int k=0;k<16;k++)assert(fabsf(expected[k]-actual[k])<1e-4f);
            ++cursor;
        }
        assert(!memcmp(native+3,corrected+3,3*64)); // retain extra native correction groups
        assert(!memcmp(memory+0xf000,native,sizeof(native))); // shared resource untouched
    }
    puts("face bind alignment: animation78 mappings -> body111..320; body/face transforms agree, native data untouched");
}
