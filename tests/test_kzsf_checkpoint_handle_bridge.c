#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_checkpoint_handle_bridge.h"
int main(void) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(code);
    const unsigned char native[]={0x48,0x8b,0x4c,0x11,8,0x48,0x8b,0x49,0x20,
        0xc1,0xe8,8,0x48,0x8b,0x1c,0xc1};
    unsigned char patch[KZSF_CHECKPOINT_HANDLE_BYTES];uintptr_t site=(uintptr_t)code+128;
    assert(kzsf_checkpoint_handle_bridge(code,KZSF_CHECKPOINT_HANDLE_STUB,(uintptr_t)code,patch,site,native));
    /* Execute patch and stub; wrapper supplies packed EAX from Windows R8D
     * and preserves host RBX. These continuations execute no guest code. */
    unsigned char *wrapper=code+256;
    const unsigned char prefix[]={0x53,0x44,0x89,0xc0,0xe8,0,0,0,0,0x5b,0xc3};
    memcpy(wrapper,prefix,sizeof(prefix));int32_t call=(int32_t)(site-((uintptr_t)wrapper+9));
    memcpy(wrapper+5,&call,4);memcpy((void*)site,patch,sizeof(patch));
    const unsigned char success[]={0x48,0x89,0xd8,0xc3},missing[]={0x31,0xc0,0xc3};
    memcpy((void*)(site+sizeof(native)),success,sizeof(success));memcpy((void*)(site+0x24),missing,sizeof(missing));
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    uintptr_t pools[16]={0},pool[8]={0},objects[2]={(uintptr_t)code+512,(uintptr_t)code+544};
    uintptr_t (*lookup)(uintptr_t,uintptr_t,uint32_t)=(uintptr_t(*)(uintptr_t,uintptr_t,uint32_t))(void*)wrapper;
    assert(lookup((uintptr_t)pools,0,0)==0);
    pools[1]=(uintptr_t)pool;pool[3]=2;
    assert(lookup((uintptr_t)pools,0,0)==0);
    pool[4]=(uintptr_t)objects;
    assert(lookup((uintptr_t)pools,0,0)==objects[0]);
    assert(lookup((uintptr_t)pools,0,0x100)==objects[1]);
    /* Inaccessible array proves bounds are checked BEFORE reading a slot. */
    pool[4]=1;
    assert(lookup((uintptr_t)pools,0,0x200)==0);
    assert(lookup((uintptr_t)pools,0,0x29f43)==0);
    assert(lookup((uintptr_t)pools,0,0xffffff00)==0);
    pool[3]=0;assert(lookup((uintptr_t)pools,0,0)==0);
    pool[3]=(uintptr_t)-1;assert(lookup((uintptr_t)pools,0,0)==0);
    pool[3]=2;pool[4]=(uintptr_t)objects;
    objects[0]=0x33;assert(lookup((uintptr_t)pools,0,0)==0);
    objects[0]=0;assert(lookup((uintptr_t)pools,0,0)==0);
    objects[0]=(uintptr_t)code+512;
    assert(lookup((uintptr_t)pools,32,0)==0);
    pools[5]=(uintptr_t)pool;assert(lookup((uintptr_t)pools,32,0)==objects[0]);
    unsigned char bad[sizeof(native)];memcpy(bad,native,sizeof(native));bad[15]^=1;
    assert(!kzsf_checkpoint_handle_bridge(code,KZSF_CHECKPOINT_HANDLE_STUB,(uintptr_t)code,patch,site,bad));
    assert(!kzsf_checkpoint_handle_bridge(code,KZSF_CHECKPOINT_HANDLE_STUB-1,(uintptr_t)code,patch,site,native));
    VirtualFree(code,0,MEM_RELEASE);puts("checkpoint guard: pool, bounds, low slots and valid lookups passed");
}
