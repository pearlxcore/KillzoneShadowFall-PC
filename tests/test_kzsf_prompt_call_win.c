/* Execute the actual Windows breakpoint/CALL replay against owned test code.
 * No game is launched. Verifies the return stack and carry flag on the CPU. */
#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_prompt_pc_win.h"
static unsigned char *code;
static int write_guest(void *unused,uintptr_t address,const void *data,size_t size) {
    (void)unused;SIZE_T written=0;
    return WriteProcessMemory(GetCurrentProcess(),(void *)address,data,size,&written) && written==size;
}
static LONG CALLBACK replay(EXCEPTION_POINTERS *info) {
    if(info->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    for(unsigned i=0;i<4;++i) if(info->ExceptionRecord->ExceptionAddress==code+kzsf_pc_calls[i]) {
        if(kzsf_pc_replay_call(info->ContextRecord,(uintptr_t)code,i,write_guest,NULL))
            return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(void) {
    code=VirtualAlloc(NULL,0x1000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(code);
    code[0xfc4d90]=0xc3; /* synthetic native callee: RET */
    void *handler=AddVectoredExceptionHandler(1,replay);assert(handler);
    for(unsigned i=0;i<4;++i) {
        uint32_t at=kzsf_pc_calls[i];
        const unsigned char before[]={0x48,0xb8,0x44,0x33,0x22,0x11,0,0,0,0,0xf9}; /* MOV RAX; STC */
        const unsigned char after[]={0x0f,0x92,0xc0,0x0f,0xb6,0xc0,0xc3}; /* SETC AL; MOVZX EAX,AL; RET */
        memcpy(code+at-sizeof(before),before,sizeof(before));
        code[at]=0xcc;memset(code+at+1,0x90,4);memcpy(code+at+5,after,sizeof(after));
        assert(FlushInstructionCache(GetCurrentProcess(),code,0x1000000));
        assert(((int (*)(void))(code+at-sizeof(before)))()==1);
    }
    assert(RemoveVectoredExceptionHandler(handler));assert(VirtualFree(code,0,MEM_RELEASE));
    puts("Windows native CALL replay: all four return paths and carry preservation pass.");
}
