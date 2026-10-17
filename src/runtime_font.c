/* Minimal libSceFont provider.
 *
 * KZSF calls into the PS4 font library during initialization. This host provider
 * logs each call and reports success so initialization can proceed. Glyph
 * rasterization is not implemented yet: text will be blank until the metrics and
 * glyph entry points produce real data. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>

static unsigned calls;

#define FONT_STUB(fn, label) \
    static ABI int32_t fn(void *a, void *b, void *c, void *d, void *e, void *f) { \
        (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; \
        printf("Runtime: %s (STUB) called\n", label); ++calls; return 0; \
    }

FONT_STUB(font_memory_init,      "sceFontMemoryInit")
FONT_STUB(font_create_library,   "sceFontCreateLibraryWithEdition")
FONT_STUB(font_open_fontset,     "sceFontOpenFontSet")
FONT_STUB(font_set_scale,        "sceFontSetScalePixel")
FONT_STUB(font_char_metrics,     "sceFontGetCharGlyphMetrics")
FONT_STUB(font_support_system,   "sceFontSupportSystemFonts")
FONT_STUB(font_close,            "sceFontCloseFont")
FONT_STUB(font_destroy_library,  "sceFontDestroyLibrary")
FONT_STUB(font_memory_term,      "sceFontMemoryTerm")
FONT_STUB(font_generate_glyph,   "sceFontGenerateCharGlyph")
FONT_STUB(font_glyph_outline,    "sceFontGlyphRefersOutline")
FONT_STUB(font_delete_glyph,     "sceFontDeleteGlyph")
FONT_STUB(font_select_ft,        "sceFontSelectLibraryFt")

uintptr_t runtime_font_resolve(const char *name) {
    if (!strcmp(name,"whrS4oksXc4#libSceFont"))   return (uintptr_t)font_memory_init;
    if (!strcmp(name,"n590hj5Oe-k#libSceFont"))   return (uintptr_t)font_create_library;
    if (!strcmp(name,"cKYtVmeSTcw#libSceFont"))   return (uintptr_t)font_open_fontset;
    if (!strcmp(name,"N1EBMeGhf7E#libSceFont"))   return (uintptr_t)font_set_scale;
    if (!strcmp(name,"L97d+3OgMlE#libSceFont"))   return (uintptr_t)font_char_metrics;
    if (!strcmp(name,"SsRbbCiWoGw#libSceFont"))   return (uintptr_t)font_support_system;
    if (!strcmp(name,"vzHs3C8lWJk#libSceFont"))   return (uintptr_t)font_close;
    if (!strcmp(name,"FXP359ygujs#libSceFont"))   return (uintptr_t)font_destroy_library;
    if (!strcmp(name,"h6hIgxXEiEc#libSceFont"))   return (uintptr_t)font_memory_term;
    if (!strcmp(name,"C-4Qw5Srlyw#libSceFont"))   return (uintptr_t)font_generate_glyph;
    if (!strcmp(name,"R1T4i+DOhNY#libSceFont"))   return (uintptr_t)font_glyph_outline;
    if (!strcmp(name,"LHDoRWVFGqk#libSceFont"))   return (uintptr_t)font_delete_glyph;
    if (!strcmp(name,"oM+XCzVG3oM#libSceFontFt")) return (uintptr_t)font_select_ft;
    return 0;
}

void runtime_font_report(void) {
    printf("Runtime: font calls=%u\n", calls);
}
