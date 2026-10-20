/* Reuse the mesh graph fixture, including real cycle/shared-leaf cases. */
#define main mesh_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_swap.h"
static unsigned char action_code[20];static int changed;
static int swap_read(void *opaque,uintptr_t p,void *out,size_t n) {
    if(p==origin+0x667900 && n==20){memcpy(out,action_code,n);return 1;}
    if(p==origin+0x3b40f84 && n==4){uint32_t stride=64;memcpy(out,&stride,4);return 1;}
    return read_memory(opaque,p,out,n);
}
static void KZSF_CHARACTER_ABI change(const uintptr_t *action,uintptr_t owner) {
    assert(owner==origin && !action[0] && !action[1] && !action[2]);
    uintptr_t current=0;memcpy(&current,memory+0x200c8,8);assert(current==action[3]);
    ptr(0x200c8,action[4]);changed++;
}
static void KZSF_CHARACTER_ABI change_first(const uintptr_t *action,uintptr_t owner) {
    assert(owner==origin);int32_t count=0;memcpy(&count,memory+0x1958,4);
    for(int32_t i=0;i<count;i++) {
        uintptr_t part=0;size_t slot=0x200c8+(size_t)i*0xf0;memcpy(&part,memory+slot,8);
        if(part==action[3]){ptr(slot,action[4]);changed++;return;}
    }
    assert(0);
}
static void swap_setup(void) {
    setup();changed=0;
    ptr(0x6018,origin+0x8000);ptr(0x7018,origin+0x9200);memory[0x6030]=memory[0x7030]=1;
    ptr(0xb130,origin+0x18000);
    for(int i=0;i<32;i++)ptr(0xe000+i*8,origin+0x1a000);
    integer(0x1a008,4);memcpy(memory+0x1a010,"bone",4);
    ptr(0x200,origin+0x1900);ptr(0x1938,origin);integer(0x1958,1);ptr(0x1960,origin+0x20000);
    ptr(0x200c8,origin+0x6000);
}
int main(void) {
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&size,8,1,f)==1);
    assert(!fseek(f,0,SEEK_END));long offset=ftell(f)-(long)size;
    assert(!fseek(f,offset+0x667900,SEEK_SET));assert(fread(action_code,1,20,f)==20);fclose(f);
    const char *why=NULL;swap_setup();unsigned char before[sizeof(memory)];memcpy(before,memory,sizeof(before));
    assert(kzsf_character_swap_compatible(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why));
    assert(!memcmp(before,memory,sizeof(before)) && !changed);
    float different=0.1f;memcpy(memory+0x18000,&different,4);
    assert(!kzsf_character_swap_compatible(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why));
    assert(!strcmp(why,"inverse-bind-differs") && !changed);
    KzsfCharacterBindingCheck check;
    assert(!kzsf_character_swap_compatible_ex(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why,&check));
    assert(check.bone==0 && check.stride==64 && check.byte==0 && check.reference_value==0 && check.candidate_value==different);
    assert(check.delta>check.tolerance);
    swap_setup();float rounding=5e-7f;memcpy(memory+0x18000,&rounding,4);
    assert(kzsf_character_swap_compatible(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why));
    float invalid=NAN;memcpy(memory+0x18000,&invalid,4);
    assert(!kzsf_character_swap_compatible(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why));
    assert(!strcmp(why,"inverse-bind-nonfinite"));
    swap_setup();ptr(0xb118,origin+0x1b000);
    for(int i=0;i<32;i++)ptr(0x1b000+i*8,origin+0x1c000);
    integer(0x1c008,4);memcpy(memory+0x1c010,"xxxx",4);
    assert(!kzsf_character_swap_compatible_ex(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why,&check));
    assert(!strcmp(why,"bone-name-differs") && check.bone==0);
    integer(0x1c008,3);
    assert(!kzsf_character_swap_compatible_ex(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why,&check));
    assert(!strcmp(why,"bone-name-length-differs"));
    swap_setup();ptr(0x7028,origin+0x1000);
    assert(!kzsf_character_swap_compatible(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why));
    swap_setup();ptr(0x9258,origin+0xc100);ptr(0xc100,origin+0x3c1ec70);
    assert(!kzsf_character_swap_compatible(origin+0x6000,origin+0x7000,origin,swap_read,NULL,type_of,&why));
    swap_setup();assert(kzsf_character_change_part(origin,origin+0x6000,origin+0x7000,origin,swap_read,NULL,change,&why));
    assert(changed==1);
    assert(kzsf_character_change_part(origin,origin+0x7000,origin+0x6000,origin,swap_read,NULL,change,&why));
    assert(changed==2);
    swap_setup();ptr(0x1938,origin+1);
    assert(!kzsf_character_change_part(origin,origin+0x6000,origin+0x7000,origin,swap_read,NULL,change,&why) && !changed);
    swap_setup();integer(0x1958,4);ptr(0x201b8,origin+0x6000);ptr(0x202a8,origin+0x8000);ptr(0x20398,origin+0x7000);
    assert(kzsf_character_change_body_parts(origin,origin+0x6000,origin+0x7000,origin,swap_read,NULL,change_first,&why));
    assert(changed==2);
    assert(kzsf_character_change_body_parts(origin,origin+0x7000,origin+0x6000,origin,swap_read,NULL,change_first,&why));
    assert(changed==5);
    uintptr_t untouched=0;memcpy(&untouched,memory+0x202a8,8);assert(untouched==origin+0x8000);
    swap_setup();integer(0x1958,2);ptr(0x201b8,origin+0x6000);
    assert(!kzsf_character_change_part(origin,origin+0x6000,origin+0x7000,origin,swap_read,NULL,change,&why) && !changed);
    puts("character swap: binding/physics/skeleton rejection, native action ABI, ownership and restoration pass");
    return 0;
}
