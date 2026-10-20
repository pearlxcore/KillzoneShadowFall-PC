#define main resource_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_face.h"
int main(void) {
    memset(memory,0,sizeof(memory));
    uintptr_t owner=origin+0x15000,rep=origin+0x16000,face=origin+0x18000;
    uintptr_t original=origin+0x1000,selected=origin+0x2000,body=origin+0x7000;
    ptr(0x151d0,original);ptr(0x18020,rep);ptr(0x16198,owner);ptr(0x18018,origin+0x17000);
    ptr(0x17000,origin+0x3cdfb20);ptr(0x2000,origin+0x3caac10);
    integer(0x2648,1);ptr(0x2650,origin+0x3000);ptr(0x3008,body);ptr(0x3018,origin+0x17000);
    unsigned char before[sizeof(memory)];memcpy(before,memory,sizeof(before));
    assert(kzsf_character_face_animation_source(owner,face,owner,body,selected,origin,read_memory,NULL,type_of)==selected);
    assert(!memcmp(before,memory,sizeof(before)));
    assert(kzsf_character_face_animation_source(owner,face,owner+8,body,selected,origin,read_memory,NULL,type_of)==original);
    assert(kzsf_character_face_animation_source(owner,face,owner,body+8,selected,origin,read_memory,NULL,type_of)==original);
    ptr(0x3018,origin+0x17500);
    assert(kzsf_character_face_animation_source(owner,face,owner,body,selected,origin,read_memory,NULL,type_of)==original);
    ptr(0x3018,origin+0x17000);integer(0x2648,65);
    assert(kzsf_character_face_animation_source(owner,face,owner,body,selected,origin,read_memory,NULL,type_of)==original);
    puts("face animation source: selected character only for exact owned body/face; NPC/default and malformed paths preserved");
}
