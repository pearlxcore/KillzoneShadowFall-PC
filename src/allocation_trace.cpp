// Optional Windows capture before C++ exception unwinding. No debugger needed.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <typeinfo>
#include <atomic>

extern "C" [[noreturn]] void __real___cxa_throw(void*, std::type_info*, void (*)(void*));

static void capture_allocation(const char* type, const char* path) noexcept {
    static std::atomic_flag active = ATOMIC_FLAG_INIT;
    if (active.test_and_set()) return;
    void* frames[64];
    const USHORT count=CaptureStackBackTrace(0,64,frames,nullptr);
    std::fprintf(stderr,"ALLOCATION_TRACE: %s; %u frames before unwinding\n",type,unsigned(count));
    for (USHORT i=0;i<count;++i) {
        MEMORY_BASIC_INFORMATION info{};
        char module[MAX_PATH]{};
        if (VirtualQuery(frames[i],&info,sizeof(info))) {
            GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase),module,MAX_PATH);
        }
        const auto offset=reinterpret_cast<ULONG_PTR>(frames[i])-reinterpret_cast<ULONG_PTR>(info.AllocationBase);
        std::fprintf(stderr,"  #%u pc=%p base=%p rva=0x%llx %s\n",unsigned(i),frames[i],info.AllocationBase,
                     static_cast<unsigned long long>(offset),module);
    }
    std::fflush(stderr);
    // Load the system copy explicitly; no process attach or external debugger.
    wchar_t library[MAX_PATH];
    const UINT n=GetSystemDirectoryW(library,MAX_PATH);
    if (n && n+13<MAX_PATH) {
        std::wcscat(library,L"\\dbghelp.dll");
        HMODULE dll=LoadLibraryW(library);
        if (dll) {
            auto write=reinterpret_cast<decltype(&MiniDumpWriteDump)>(GetProcAddress(dll,"MiniDumpWriteDump"));
            HANDLE file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (write && file!=INVALID_HANDLE_VALUE) {
                CONTEXT context{};
                RtlCaptureContext(&context);
                EXCEPTION_RECORD record{};
                record.ExceptionCode=0xe0000001; // Synthetic snapshot marker, not the original exception code.
                record.ExceptionAddress=count?frames[0]:nullptr;
                EXCEPTION_POINTERS pointers{&record,&context};
                MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(),&pointers,FALSE};
                const BOOL ok=write(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,&exception,nullptr,nullptr);
                std::fprintf(stderr,"ALLOCATION_TRACE: dump %s: %s (Win32 %lu)\n",path,ok?"saved":"failed",ok?0ul:GetLastError());
            } else {
                std::fprintf(stderr,"ALLOCATION_TRACE: cannot open dump/writer (Win32 %lu)\n",GetLastError());
            }
            if (file!=INVALID_HANDLE_VALUE) CloseHandle(file);
            FreeLibrary(dll);
        }
    }
    std::fflush(stderr);
    active.clear();
}

extern "C" [[noreturn]] void __wrap___cxa_throw(void* object,std::type_info* type,void (*destroy)(void*)) {
    const char* path=std::getenv("KZSF_ALLOCATION_DUMP");
    if (path && *path && type && std::strstr(type->name(),"bad_alloc")) {
        capture_allocation(type->name(),path);
    }
    __real___cxa_throw(object,type,destroy);
}
