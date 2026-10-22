#define main mesh_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_body_visibility.h"
static int begins,ends,toggles;
static void KZSF_CHARACTER_ABI begin(uintptr_t model,unsigned flags){assert(model==origin+0x5000 && flags==0x20000);begins++;}
static void KZSF_CHARACTER_ABI slot(uintptr_t model,int index,int enabled) {
    assert(model==origin+0x5000 && (enabled==0 || enabled==1));toggles++;
    uint32_t flags;memcpy(&flags,memory+0x20000+(size_t)index*0xf0+0xe0,4);
    integer(0x20000+(size_t)index*0xf0+0xe0,(flags&~0x100u)|(enabled ? 0x100u : 0));
}
static void KZSF_CHARACTER_ABI end(uintptr_t model){assert(model==origin+0x5000);ends++;}
int main(void) {
    setup();ptr(0x200,origin+0x5000);ptr(0x5038,origin);integer(0x5058,9);ptr(0x5060,origin+0x20000);
    for(int i=0;i<9;i++){ptr(0x20000+(size_t)i*0xf0+0xc8,origin+(i<7 ? 0x7000 : i==7 ? 0x8000 : 0x9000));integer(0x20000+(size_t)i*0xf0+0xe0,0x702);}
    KzsfCharacterBodyVisibility changes[128];uintptr_t model=0;int n=0;
    assert(kzsf_character_body_visibility_plan(origin,origin+0x7000,origin+0x8000,read_memory,NULL,&model,changes,&n) && n==7);
    assert(changes[0].index==1 && changes[6].index==7);
    assert(kzsf_character_body_visibility_apply(origin,model,changes,n,read_memory,NULL,begin,slot,end));
    assert(begins==1 && ends==1 && toggles==7);
    assert(kzsf_character_body_visibility_plan(origin,origin+0x7000,origin+0x8000,read_memory,NULL,&model,changes,&n) && n==0);
    uint32_t untouched;memcpy(&untouched,memory+0x20000+8*0xf0+0xe0,4);assert(untouched==0x702);
    integer(0x200e0,0x602);
    assert(kzsf_character_body_visibility_plan(origin,origin+0x7000,origin+0x8000,read_memory,NULL,&model,changes,&n) && n==0);
    changes[0]=(KzsfCharacterBodyVisibility){1,origin+0xdead,0};
    assert(!kzsf_character_body_visibility_apply(origin,model,changes,1,read_memory,NULL,begin,slot,end));
    assert(begins==2 && ends==2 && toggles==7);
    /* Checkpoint resume: every chosen-body copy disabled, attachment remains
     * visible. Recover one slot only when normal third-person allows it. */
    assert(kzsf_character_body_visibility_plan_ex(origin,origin+0x7000,origin+0x8000,0,read_memory,NULL,&model,changes,&n) && n==0);
    assert(kzsf_character_body_visibility_plan_ex(origin,origin+0x7000,origin+0x8000,1,read_memory,NULL,&model,changes,&n) && n==1);
    assert(changes[0].index==0 && changes[0].enabled==1);
    assert(kzsf_character_body_visibility_apply(origin,model,changes,n,read_memory,NULL,begin,slot,end));
    assert(kzsf_character_body_visibility_plan_ex(origin,origin+0x7000,origin+0x8000,1,read_memory,NULL,&model,changes,&n) && n==0);
    memcpy(&untouched,memory+0x20000+8*0xf0+0xe0,4);assert(untouched==0x702);
    /* Default player before any model swap has no 'other' recovery token. */
    integer(0x200e0,0x602);
    assert(kzsf_character_body_visibility_plan_ex(origin,origin+0x7000,0,1,read_memory,NULL,&model,changes,&n) && n==1 && changes[0].enabled);
    /* Missing desired resource must not reveal attachments or another body. */
    assert(kzsf_character_body_visibility_plan_ex(origin,origin+0xdead,0,1,read_memory,NULL,&model,changes,&n) && n==0);
    /* Enabled alternate body is disabled in the same recovery transaction. */
    integer(0x20000+7*0xf0+0xe0,0x702);
    assert(kzsf_character_body_visibility_plan_ex(origin,origin+0x7000,origin+0x8000,1,read_memory,NULL,&model,changes,&n) && n==2);
    assert(changes[0].index==0 && changes[0].enabled && changes[1].index==7 && !changes[1].enabled);
    assert(kzsf_character_body_visibility_apply(origin,model,changes,n,read_memory,NULL,begin,slot,end));
    /* Falling/zipline: chosen body disabled, native body re-enabled while
     * the normal camera show-body flag is false. Transfer visibility to CJ. */
    integer(0x200e0,0x602);integer(0x20000+7*0xf0+0xe0,0x702);
    assert(kzsf_character_body_visibility_plan_ex(origin,origin+0x7000,origin+0x8000,0,read_memory,NULL,&model,changes,&n) && n==0);
    assert(kzsf_character_body_visibility_plan_follow(origin,origin+0x7000,origin+0x8000,0,1,read_memory,NULL,&model,changes,&n) && n==2);
    assert(changes[0].index==0 && changes[0].enabled && changes[1].index==7 && !changes[1].enabled);
    assert(kzsf_character_body_visibility_apply(origin,model,changes,n,read_memory,NULL,begin,slot,end));
    memcpy(&untouched,memory+0x20000+8*0xf0+0xe0,4);assert(untouched==0x702);
    /* A genuinely hidden native state must stay hidden. */
    integer(0x200e0,0x602);
    assert(kzsf_character_body_visibility_plan_follow(origin,origin+0x7000,origin+0x8000,0,1,read_memory,NULL,&model,changes,&n) && n==0);
    ptr(0x5038,origin+1);
    assert(!kzsf_character_body_visibility_plan(origin,origin+0x7000,origin+0x8000,read_memory,NULL,&model,changes,&n));
    puts("body visibility: checkpoint recovery, default/selected bodies, hidden-state gate, duplicate and attachment isolation passed");
    return 0;
}
