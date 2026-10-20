#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_follow_baseline.h"
#include "../src/kzsf_third_person.h"
static int remote(void *opaque,uintptr_t address,void *out,size_t size) {
    SIZE_T got=0;return ReadProcessMemory((HANDLE)opaque,(void*)address,out,size,&got) && got==size;
}
static uintptr_t type(uintptr_t object,uintptr_t base,KzsfZoomRead read,void *opaque) {
    uintptr_t table=0,getter=0;unsigned char code[8];int32_t rel=0;
    if(!object || !read(opaque,object,&table,8) || !table || !read(opaque,table,&getter,8) || getter<base ||
       getter-base>0x1190000-8 || !read(opaque,getter,code,8) || code[0]!=0x48 || code[1]!=0x8d || code[2]!=5 || code[7]!=0xc3)return 0;
    memcpy(&rel,code+3,4);return getter+7+rel;
}
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    HANDLE process=OpenProcess(PROCESS_VM_READ,FALSE,(DWORD)strtoul(argv[1],0,0));if(!process)return 3;
    uintptr_t view=strtoull(argv[2],0,0),render=strtoull(argv[3],0,0),base=UINT64_C(0x800000000);
    KzsfFollowBaseline b={0};const char *why="unknown";
    int ok=kzsf_follow_baseline_read(view,render,base,remote,process,type,&b,&why);
    float native[16],pose[16];KzsfThirdPerson follow={.requested=1};
    int preview=ok && remote(process,render+0x10,native,sizeof(native)) &&
        kzsf_third_person_step(&follow,view,100,1,0,ok,native,pose);
    printf("{\"read_only\":true,\"atomic\":false,\"baseline\":%d,\"local_pose_preview\":%d,\"reason\":\"%s\",\"view\":\"%llx\",\"render\":\"%llx\",\"owner\":\"%llx\",\"state\":\"%llx\",\"state_type\":\"%llx\"}\n",
        ok,preview,why,(unsigned long long)view,(unsigned long long)render,(unsigned long long)b.owner,
        (unsigned long long)b.state,(unsigned long long)type(b.state,base,remote,process));
    CloseHandle(process);return 0;
}
