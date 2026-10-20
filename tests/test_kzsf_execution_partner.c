#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_execution_partner.h"
static unsigned char memory[0x6000];
static const uintptr_t base=0x10000000;
static int race;
static void pointer(size_t offset,uintptr_t value){memcpy(memory+offset,&value,8);}
static int read_memory(void *opaque,uintptr_t address,void *out,size_t n) {
    (void)opaque;
    if(address<base || address-base>sizeof(memory) || n>sizeof(memory)-(address-base))return 0;
    memcpy(out,memory+address-base,n);
    if(race && address==base+0x1108)pointer(0x1108,base+0x4048);
    return 1;
}
static uintptr_t type(uintptr_t object,uintptr_t image,KzsfZoomRead read,void *opaque) {
    uintptr_t result=0;(void)image;
    return read(opaque,object,&result,8) ? result : 0;
}
static KzsfFollowBaseline setup(void) {
    memset(memory,0,sizeof(memory));race=0;
    pointer(0x1000,base+0x3ced350);pointer(0x1038,base+0x2000);
    pointer(0x1088,base+0x5018);memory[0x10dd]=1;pointer(0x1108,base+0x3048);
    pointer(0x3000,base+0x3ca98b0);pointer(0x38f8,base+0x4000);
    pointer(0x4000,base+0x3ced350);pointer(0x4038,base+0x3000);
    KzsfFollowBaseline b={.owner=base+0x2000,.combat_component=base+0x1000,.combat_layer=base+0x5000};
    return b;
}
static uintptr_t partner(KzsfFollowBaseline *b) {
    return kzsf_execution_partner(b,base,read_memory,NULL,type);
}
int main(void) {
    KzsfFollowBaseline b=setup();unsigned char saved[sizeof(memory)];memcpy(saved,memory,sizeof(memory));
    assert(partner(&b)==base+0x3000);assert(!memcmp(saved,memory,sizeof(memory)));
    b.combat_layer=0;assert(!partner(&b));
    b=setup();memory[0x10dd]=0;assert(!partner(&b)); /* Fade no longer grants exclusion. */
    b=setup();memory[0x10dd]=2;assert(!partner(&b));
    b=setup();pointer(0x1038,base+0x3000);assert(!partner(&b));
    b=setup();pointer(0x1088,base+0x5030);assert(!partner(&b));
    b=setup();pointer(0x1108,base+0x2048);assert(!partner(&b)); /* Never self. */
    b=setup();pointer(0x1108,0);assert(!partner(&b));
    b=setup();pointer(0x1108,0x48);assert(!partner(&b));
    b=setup();pointer(0x3000,base+0x3cbf2a8);assert(!partner(&b));
    b=setup();pointer(0x38f8,0);assert(!partner(&b));
    b=setup();pointer(0x4000,base+0x3ca98b0);assert(!partner(&b));
    b=setup();pointer(0x4038,base+0x2000);assert(!partner(&b));
    b=setup();race=1;assert(!partner(&b));
    b=setup();b.combat_layer=UINTPTR_MAX;assert(!partner(&b));
    assert(!kzsf_execution_partner(NULL,base,read_memory,NULL,type));
    puts("Execution partner: owned active action, paired Soldier/component, race/type/handle rejection and read-only checks pass.");
}
