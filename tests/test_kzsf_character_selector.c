#define main mesh_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_selector.h"
int main(void) {
    KzsfCharacterChoice choices[KZSF_CHARACTER_SELECTION_LIMIT];int32_t count=0;
    assert(kzsf_character_choice_add(choices,&count,3,"VSA",2));
    assert(kzsf_character_choice_add(choices,&count,1,"HGH",1));
    assert(kzsf_character_choice_add(choices,&count,2,"HGH",0));
    assert(count==3 && choices[0].part==2 && choices[1].part==1 && choices[2].part==3);
    assert(kzsf_character_choice_add(choices,&count,1,"alias",9) && count==4);
    assert(kzsf_character_choice_add(choices,&count,1,"alias",10) && count==4);
    assert(!kzsf_character_choice_add(choices,&count,99,"HGH",0) && count==4);
    assert(!strcmp(choices[3].name,"alias") && choices[3].variant==9);
    assert(kzsf_character_choice_add(choices,&count,1,"civilian10",0) && count==5);
    unsigned char eligible[]={0,1,0,1};
    assert(kzsf_character_choice_next(-1,4,eligible)==1);
    assert(kzsf_character_choice_next(1,4,eligible)==3);
    assert(kzsf_character_choice_next(3,4,eligible)==-1);
    assert(kzsf_character_choice_next(-2,4,eligible)==-1);
    assert(kzsf_character_choice_next(-1,0,eligible)==-1);
    /* All15 civilian identities survive even though only8 body pointers are
     * unique. Their faces are selected by resource name in the caller. */
    count=0;
    for(int i=0;i<15;i++) {
        char name[64];snprintf(name,sizeof(name),"vsa_Civilians_Female_%02d",i+1);
        assert(kzsf_character_choice_add(choices,&count,(uintptr_t)(1+i%8),name,0));
    }
    assert(count==15);
    for(int i=0;i<15;i++){char name[64];snprintf(name,sizeof(name),"vsa_Civilians_Female_%02d",i+1);assert(!strcmp(choices[i].name,name));}
    count=KZSF_CHARACTER_SELECTION_LIMIT;
    for(int i=0;i<count;i++)choices[i].part=1;
    assert(!kzsf_character_choice_add(choices,&count,99,"overflow",0));
    setup();integer(0x1a008,4);memcpy(memory+0x1a010,"bone",4);
    integer(0xc030,2);ptr(0xc038,origin+0x21000);
    integer(0xc130,3);ptr(0xc138,origin+0x22000);
    for(int i=0;i<3;i++) {
        ptr(0x22000+i*0x18,origin+0x1a000);
        if(i<2)ptr(0x21000+i*0x18,origin+0x1a000);
    }
    unsigned char before[sizeof(memory)];memcpy(before,memory,sizeof(before));
    KzsfCharacterRigAudit r;
    assert(kzsf_character_joint_prefix(origin+0xc000,origin+0xc100,read_memory,NULL,&r));
    assert(r.reference_count==2 && r.candidate_count==3 && !memcmp(before,memory,sizeof(before)));
    memory[0x22028]=1;
    assert(!kzsf_character_joint_prefix(origin+0xc000,origin+0xc100,read_memory,NULL,&r));
    assert(!strcmp(r.reason,"joint-parent-differs") && r.joint==1);
    memory[0x22028]=0;integer(0xc130,1);
    assert(!kzsf_character_joint_prefix(origin+0xc000,origin+0xc100,read_memory,NULL,&r));
    integer(0xc130,3);ptr(0x22018,origin+0x1c000);
    integer(0x1c008,4);memcpy(memory+0x1c010,"xxxx",4);
    assert(!kzsf_character_joint_prefix(origin+0xc000,origin+0xc100,read_memory,NULL,&r));
    assert(!strcmp(r.reason,"joint-name-differs") && r.joint==1);
    integer(0x1c008,999);
    assert(!kzsf_character_joint_prefix(origin+0xc000,origin+0xc100,read_memory,NULL,&r));
    assert(!strcmp(r.reason,"joint-name-header"));
    puts("character selector: deterministic ordering, dedup/limit and read-only joint-prefix rejection pass");
    return 0;
}
