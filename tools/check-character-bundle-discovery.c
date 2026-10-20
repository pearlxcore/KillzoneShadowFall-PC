/* Read-only verification of the exact selector discovery helper on a live
 * retained bundle. No guest calls, writes, injection or thread suspension. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_character_bundle_loader.h"
static HANDLE process;
static unsigned reads;
static int memory(void *opaque,uintptr_t address,void *out,size_t size){
    (void)opaque;SIZE_T received=0;
    return ++reads<=100000 && size<=65536 && ReadProcessMemory(process,(void*)address,out,size,&received) && received==size;
}
static uintptr_t type(uintptr_t object,uintptr_t base,KzsfZoomRead read,void *opaque){
    uintptr_t table=0,getter=0;unsigned char code[8];int32_t displacement=0;
    if(!kzsf_zoom_read(read,opaque,object,0,&table,8) || !kzsf_zoom_read(read,opaque,table,0,&getter,8) ||
       getter<base || getter-base>0x1190000-8 || !read(opaque,getter,code,8) ||
       code[0]!=0x48 || code[1]!=0x8d || code[2]!=5 || code[7]!=0xc3)return 0;
    memcpy(&displacement,code+3,4);return getter+7+displacement;
}
int main(int argc,char **argv){
    if(argc<4 || argc%2!=0)return 2;
    process=OpenProcess(PROCESS_VM_READ,FALSE,(DWORD)strtoul(argv[1],NULL,0));if(!process)return 3;
    uintptr_t base=UINT64_C(0x800000000),manager=0;
    if(!memory(NULL,base+0x3c67760,&manager,8)){CloseHandle(process);return 4;}
    int passed=1;
    for(int i=2;i<argc;i+=2){
        int model=atoi(argv[i]);KzsfCharacterBundleLease lease={0};uintptr_t soldier=0;
        lease.manager=manager;lease.entry=(uintptr_t)strtoull(argv[i+1],NULL,0);lease.queued=1;
        int state=kzsf_character_bundle_soldier(model,base,memory,NULL,type,&lease,&soldier);
        printf("%s state=%d soldier=0x%llx resources=%d read_only=1\n",kzsf_character_model_name(model),state,
            (unsigned long long)soldier,lease.resource_count);
        if(state!=1)passed=0;
    }
    CloseHandle(process);return passed ? 0 : 1;
}
