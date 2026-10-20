#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 2 && argc != 4) return 2;
    DWORD pid = strtoul(argv[1], 0, 10);
    HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (argc == 4 && process) {
        unsigned long long address = strtoull(argv[2], 0, 0);
        size_t length = strtoul(argv[3], 0, 0);
        if (length > 65536) return 4;
        unsigned char *data = calloc(1, length); SIZE_T count = 0;
        ReadProcessMemory(process, (void*)address, data, length, &count);
        for (size_t i=0; i<count; ++i) {
            if (i%16 == 0) printf("\n%llx:", address+i);
            printf(" %02x", data[i]);
        }
        puts(""); free(data); CloseHandle(process); return count == length ? 0 : 5;
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (!process || snap == INVALID_HANDLE_VALUE) return 3;
    THREADENTRY32 entry = {.dwSize = sizeof(entry)};
    if (Thread32First(snap, &entry)) do {
        if (entry.th32OwnerProcessID != pid) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        PWSTR name = NULL;
        GetThreadDescription(thread, &name);
        if (SuspendThread(thread) != (DWORD)-1) {
            CONTEXT ctx = {.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER};
            if (GetThreadContext(thread, &ctx)) {
                unsigned long long stack[512] = {0}; SIZE_T count = 0;
                ReadProcessMemory(process, (void*)ctx.Rsp, stack, sizeof(stack), &count);
                printf("tid=%lu name=%ls rip=%llx rsp=%llx rax=%llx rcx=%llx rdi=%llx\n", entry.th32ThreadID, name ? name : L"", ctx.Rip, ctx.Rsp, ctx.Rax, ctx.Rcx, ctx.Rdi);
                printf(" regs: rbx=%llx rbp=%llx rsi=%llx r12=%llx r13=%llx r14=%llx r15=%llx\n", ctx.Rbx, ctx.Rbp, ctx.Rsi, ctx.R12, ctx.R13, ctx.R14, ctx.R15);
                printf(" stack:");
                for (size_t i=0; i<count/8; ++i)
                    if (stack[i] && (i < 16 || (stack[i] >= 0x800000000ULL && stack[i] < 0x804000000ULL)))
                        printf(" %zu:%llx", i, stack[i]);
                puts("");
            }
            ResumeThread(thread);
        }
        if (name) LocalFree(name);
        CloseHandle(thread);
    } while (Thread32Next(snap, &entry));
    CloseHandle(snap); CloseHandle(process); return 0;
}
