#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/compat_win.h"

static unsigned char *destination;
static int faults;
static LONG CALLBACK unprotect(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        e->ExceptionRecord->ExceptionInformation[0] != 1 ||
        e->ExceptionRecord->ExceptionInformation[1] < (ULONG_PTR)destination ||
        e->ExceptionRecord->ExceptionInformation[1] >= (ULONG_PTR)destination+4096)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD old;
    if (!VirtualProtect(destination,4096,PAGE_READWRITE,&old)) return EXCEPTION_CONTINUE_SEARCH;
    ++faults;
    return EXCEPTION_CONTINUE_EXECUTION;
}
int main(void) {
    char name[MAX_PATH], dir[MAX_PATH];
    assert(GetTempPathA(MAX_PATH,dir));
    assert(GetTempFileNameA(dir,"kzf",0,name));
    int fd=_open(name,_O_BINARY|_O_RDWR);
    assert(fd>=0);
    assert(_write(fd,"0123456789",10)==10);
    assert(_lseeki64(fd,3,SEEK_SET)==3);
    destination=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(destination);
    void *handler=AddVectoredExceptionHandler(1,unprotect);
    assert(handler);
    DWORD old;
    assert(VirtualProtect(destination,4096,PAGE_READONLY,&old));
    ssize_t n=compat_pread(fd,destination,4,2);
    printf("protected pread: result=%lld errno=%d faults=%d\n",(long long)n,errno,faults);
    fflush(stdout);
    assert(n==4 && memcmp(destination,"2345",4)==0 && faults>0);
    assert(_lseeki64(fd,0,SEEK_CUR)==3);
    assert(compat_pread(fd,destination,8,8)==2);
    assert(memcmp(destination,"89",2)==0);
    assert(compat_pread(fd,destination,1,10)==0);
    assert(compat_pread(fd,destination,0,0)==0);
    assert(compat_pread(-1,destination,1,0)==-1 && errno==EBADF);
    assert(compat_pread(fd,destination,1,-1)==-1 && errno==EINVAL);
    _close(fd); DeleteFileA(name);
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(destination,0,MEM_RELEASE);
    puts("protected pread tests passed");
    return 0;
}
