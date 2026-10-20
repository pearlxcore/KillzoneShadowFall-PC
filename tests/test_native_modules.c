#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char nid[12];
    assert(!runtime_symbol_nid("CoreLibraryInitializer",nid));
    assert(!strcmp(nid,"vSRWtiQDUXY"));
    assert(!runtime_symbol_nid("gHtnGetPlannerEntryData_hgh_medic",nid));
    assert(!strcmp(nid,"h+ZUYvbKhLI"));
    static const char names[2][128]={"all_programs.prx","other.prx"};
    unsigned char image[64]={0};
    RuntimeNativeExport exports[]={
        {0,8,1,"vSRWtiQDUXY"}, {1,16,1,"vSRWtiQDUXY"},
        {0,24,1,"h+ZUYvbKhLI"}
    };
    runtime_native_modules(names,2,exports,3,image);
    assert(runtime_native_module_handle("/app0/all_programs.prx")==0x100);
    assert(!runtime_native_module_handle("/app0/missing.prx"));
    assert(runtime_native_symbol(0x100,"CoreLibraryInitializer")== (uintptr_t)(image+8));
    assert(runtime_native_symbol(0x101,"CoreLibraryInitializer")== (uintptr_t)(image+16));
    assert(runtime_native_symbol(0x100,"gHtnGetPlannerEntryData_hgh_medic")== (uintptr_t)(image+24));
    assert(!runtime_native_symbol(7,"CoreLibraryInitializer"));
    assert(!runtime_native_symbol(0x100,"missing"));
    exports[2]=(RuntimeNativeExport){0,32,1,"vSRWtiQDUXY"};
    assert(!runtime_native_symbol(0x100,"CoreLibraryInitializer"));
    puts("Native module resolution tests passed");
}
