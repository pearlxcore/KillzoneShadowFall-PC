/* Resolve the prelinked modules' dynamic exports using PS4's salted SHA-1 NID.
 * These tables describe the same image as the relocation data; no sidecar or
 * fixed guest base is involved. The loader retains them for process lifetime. */
#include "runtime.h"
#include <string.h>
#include <libavutil/sha.h>
#include <libavutil/mem.h>

static const char (*module_names)[128];
static const RuntimeNativeExport *module_exports;
static size_t module_count, export_count;
static const unsigned char *image_base;

void runtime_native_modules(const char (*names)[128], size_t count,
                            const RuntimeNativeExport *exports, size_t count_exports, const void *base) {
    module_names=names; module_count=count;
    module_exports=exports; export_count=count_exports; image_base=base;
}

int32_t runtime_native_module_handle(const char *path) {
    if (!path) return 0;
    const char *name=strrchr(path,'/'); name=name ? name+1 : path;
    for (size_t i=0;i<module_count;++i)
        if (!strcmp(name,module_names[i])) return (int32_t)(0x100+i);
    return 0;
}

int runtime_native_module_valid(int32_t handle) {
    return handle>=0x100 && (size_t)(handle-0x100)<module_count;
}

int runtime_symbol_nid(const char *name, char result[12]) {
    static const unsigned char salt[]={0x51,0x8d,0x64,0xa6,0x35,0xde,0xd8,0xc1,
                                       0xe6,0xb0,0x39,0xb1,0xc3,0xe5,0x52,0x30};
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    struct AVSHA *sha=av_sha_alloc();
    unsigned char digest[20];
    if (!sha) return -1;
    av_sha_init(sha,160);
    av_sha_update(sha,(const unsigned char *)name,strlen(name));
    av_sha_update(sha,salt,sizeof(salt));
    av_sha_final(sha,digest); av_free(sha);
    /* Base64 of the first eight digest bytes in reverse order, without padding. */
    uint32_t bits=0; unsigned count=0, at=0;
    for (int i=7;i>=0;--i) {
        bits=(bits<<8)|digest[i]; count+=8;
        while (count>=6) { count-=6; result[at++]=alphabet[(bits>>count)&63]; }
    }
    if (count) result[at++]=alphabet[(bits<<(6-count))&63];
    result[at]=0;
    return 0;
}

uintptr_t runtime_native_symbol(int32_t handle, const char *name) {
    if (!runtime_native_module_valid(handle) || !name) return 0;
    char nid[12];
    if (runtime_symbol_nid(name,nid)) return 0;
    uintptr_t result=0;
    for (size_t i=0;i<export_count;++i) {
        const RuntimeNativeExport *s=&module_exports[i];
        if (s->module!=(uint64_t)(handle-0x100) || strcmp(s->nid,nid)) continue;
        uintptr_t address=(uintptr_t)(image_base+s->offset);
        if (result && result!=address) return 0; /* ambiguous across libraries */
        result=address;
    }
    return result;
}
