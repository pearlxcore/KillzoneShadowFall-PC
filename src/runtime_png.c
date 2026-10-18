/* Minimal libScePngDec provider.
 *
 * PNG decoding is not implemented. The entry points resolve so the guest's image
 * loader can run, and report an unsupported-format error so it takes its own
 * "image missing" path instead of stopping on an unresolved import. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>

#define PNG_UNSUPPORTED ((int32_t)0x80690000)

static unsigned calls;

static ABI int32_t png_parse_header(const void *param, void *header) {
    (void)param; (void)header; ++calls;
    printf("Runtime: scePngDecParseHeader (unsupported format)\n");
    return PNG_UNSUPPORTED;
}
static ABI int32_t png_query_memory(const void *param) {
    (void)param; ++calls;
    return PNG_UNSUPPORTED;
}
static ABI int32_t png_create(const void *param, const void *info, void *out) {
    (void)param; (void)info; (void)out; ++calls;
    return PNG_UNSUPPORTED;
}
static ABI int32_t png_decode(void *handle, const void *info, uint32_t flags) {
    (void)handle; (void)info; (void)flags; ++calls;
    return PNG_UNSUPPORTED;
}
static ABI int32_t png_delete(void *handle) {
    (void)handle; ++calls;
    return 0;
}

uintptr_t runtime_png_resolve(const char *name) {
    if (!strcmp(name,"U6h4e5JRPaQ#libScePngDec")) return (uintptr_t)png_parse_header;
    if (!strcmp(name,"-6srIGbLTIU#libScePngDec")) return (uintptr_t)png_query_memory;
    if (!strcmp(name,"m0uW+8pFyaw#libScePngDec")) return (uintptr_t)png_create;
    if (!strcmp(name,"WC216DD3El4#libScePngDec")) return (uintptr_t)png_decode;
    if (!strcmp(name,"QbD+eENEwo8#libScePngDec")) return (uintptr_t)png_delete;
    return 0;
}

void runtime_png_report(void) {
    printf("Runtime: PngDec calls=%u\n", calls);
}
