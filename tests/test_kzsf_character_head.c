#define main resource_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_head.h"
static int toggles;
static int guarded_read(void *opaque,uintptr_t address,void *out,size_t size) {
    static const unsigned char signature[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xf8,0,0,0};
    if(address==origin+0x703260 && size==sizeof(signature)){memcpy(out,signature,size);return 1;}
    return read_memory(opaque,address,out,size);
}
static void KZSF_CHARACTER_ABI toggle(uintptr_t model,uintptr_t part,int enabled) {
    assert(model==origin+0x5000 && part==origin+0xa800);toggles++;
    integer(0x5058,2);ptr(0x201b8,part);integer(0x201d0,enabled ? 0x100 : 0);
}
int main(void) {
    setup();ptr(0x1000,origin+0x3caac10);ptr(0x1330,origin+0x10000);
    ptr(0x10000,origin+0x3c30948);ptr(0x10098,origin+0x11000);
    ptr(0x11000,origin+0x3c30b08);ptr(0x11020,origin+0xa800);
    ptr(0x6018,origin+0x9000);memory[0x6030]=1;
    ptr(0x7018,origin+0x9200);memory[0x7030]=1;
    ptr(0xc100,origin+0x3c1ec70);ptr(0x9258,origin+0xc100);
    ptr(0xa800,origin+0x3c3d7b0);ptr(0xa818,origin+0x9400);memory[0xa830]=1;
    leaf(0x9400,0xb000);
    KzsfCharacterHead head;const char *why=NULL;
    assert(kzsf_character_head_read(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why));
    assert(head.part==origin+0xa800 && !head.target_pose);
    ptr(0x9458,origin+0xc100);ptr(0x9470,origin+0xb100);
    assert(kzsf_character_head_read(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why) && head.target_pose);
    ptr(0xa828,origin+0xbeef);
    assert(!kzsf_character_head_read(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why));
    ptr(0xa828,0);ptr(0x11000,origin+0x1111);
    assert(!kzsf_character_head_read(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why));
    ptr(0x1038,origin+0x12000);ptr(0x12000,origin+0x3c3cfd0);
    integer(0x12058,2);ptr(0x12060,origin+0x13000);
    ptr(0x13000,origin+0x6000);ptr(0x13008,origin+0xa800);
    assert(kzsf_character_head_from_model(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why));
    assert(head.reference==origin+0x12000 && head.part==origin+0xa800 && head.target_pose);
    /* Two eligible extras are not arbitrarily interpreted as one head. */
    ptr(0xa900,origin+0x3c3d7b0);ptr(0xa918,origin+0x9400);memory[0xa930]=1;
    integer(0x12058,3);ptr(0x13010,origin+0xa900);
    assert(!kzsf_character_head_from_model(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why));
    integer(0x12058,129);
    assert(!kzsf_character_head_from_model(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why));
    ptr(0x12000,origin+0x1234);
    assert(!kzsf_character_head_from_model(origin+0x1000,origin+0x6000,origin+0x7000,origin,read_memory,NULL,type_of,&head,&why));
    ptr(0x200,origin+0x5000);ptr(0x5038,origin);integer(0x5058,1);ptr(0x5060,origin+0x20000);ptr(0x200c8,origin+0x7000);
    int was=-1;
    assert(kzsf_character_head_toggle(origin,origin+0xa800,1,origin,guarded_read,NULL,toggle,&was) && was==0 && toggles==1);
    assert(kzsf_character_head_toggle(origin,origin+0xa800,1,origin,guarded_read,NULL,toggle,&was) && was==1 && toggles==1);
    assert(kzsf_character_head_toggle(origin,origin+0xa800,0,origin,guarded_read,NULL,toggle,NULL) && toggles==2);
    ptr(0x5038,origin+1);
    assert(!kzsf_character_head_toggle(origin,origin+0xa800,1,origin,guarded_read,NULL,toggle,NULL) && toggles==2);
    puts("head attachment: typed state lookup, source/target bindings, physics/type refusal, owner isolation and native enable/restore pass");
    return 0;
}
