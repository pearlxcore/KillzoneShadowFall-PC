/* Real Windows placeholder views and guest placement, using a tiny backing section. */
#include "../src/runtime_memory.c"
#include <assert.h>
int *runtime_errno(void) { static __thread int value; return &value; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    (void)table; (void)count; (void)nid;
    assert(!"export lookup is not used by this memory test");
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    assert(direct_size() == strtoull(argv[1],NULL,10)*1024*1024);
    if (!kzsf_memory_layout()) {
        assert(find_free(0,PAGE,PAGE) == USER_MIN);
        puts("legacy pool/placement passed");
        return 0;
    }
    assert(find_free(0,PAGE,PAGE) == MANAGED_MIN);
    assert(find_free(MANAGED_MIN+PAGE,PAGE,PAGE) == MANAGED_MIN+PAGE);
    assert(find_free(USER_MIN+PAGE,PAGE,PAGE) == USER_MIN+PAGE);
    assert(vma_insert(0,(Vma){.start=MANAGED_MIN,.end=MANAGED_MAX}) == 0);
    assert(find_free(0,PAGE,PAGE) == FREE_MIN);
    vma_erase(0,1);
    assert(find_in(USER_MAX-PAGE,USER_MAX,PAGE*2,PAGE) == 0);
    assert(find_in(USER_MAX,USER_MAX,UINT64_MAX,PAGE) == 0);
    runtime_memory_reserve();
    section=CreateFileMapping2(INVALID_HANDLE_VALUE,NULL,FILE_MAP_ALL_ACCESS,
                              PAGE_EXECUTE_READWRITE,SEC_COMMIT,65536,NULL,NULL,0);
    assert(section);
    backing_base=MapViewOfFile3(section,GetCurrentProcess(),NULL,0,65536,0,PAGE_READWRITE,NULL,0);
    assert(backing_base);
    int64_t physical=-1;
    assert(direct_allocate(0,direct_size(),PAGE,0,0,&physical) == 0 && physical == 0);
    void *automatic=NULL;
    assert(direct_map(&automatic,PAGE,3,0,physical,0) == 0);
    assert((uintptr_t)automatic == MANAGED_MIN);
    void *fixed=(void *)USER_MIN;
    assert(direct_map(&fixed,PAGE,3,MAP_FIXED_FLAG,physical,0) == 0);
    *(volatile uint64_t *)automatic=UINT64_C(0x1234abcdef);
    assert(*(volatile uint64_t *)fixed == UINT64_C(0x1234abcdef));
    assert(direct_unmap(automatic,PAGE) == 0);
    automatic=NULL;
    assert(direct_map(&automatic,PAGE,3,0,physical,0) == 0);
    assert((uintptr_t)automatic == MANAGED_MIN);
    assert(*(volatile uint64_t *)automatic == UINT64_C(0x1234abcdef));
    assert(direct_release(physical,PAGE) == 0);
    assert(!runtime_memory_is_mapped((uintptr_t)automatic,PAGE));
    assert(!runtime_memory_is_mapped((uintptr_t)fixed,PAGE));
    puts("beta placement, fixed alias, unmap/remap and release passed");
    return 0;
}
