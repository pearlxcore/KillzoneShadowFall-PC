#ifndef KZSF_CUSTOM_MATERIAL_MAPS_H
#define KZSF_CUSTOM_MATERIAL_MAPS_H
#include <string.h>
/* Explicit roles for the observed weighted-body material. The lit sphere is
 * a lighting lookup, not an atlas painted on the native character. */
enum {KZSF_MAP_UNKNOWN=-1,KZSF_MAP_COLOR,KZSF_MAP_NORMAL,KZSF_MAP_MASK,
      KZSF_MAP_ROUGHNESS,KZSF_MAP_WHITE,KZSF_MAP_LIGHTING};
static inline int kzsf_custom_material_map_role(const char *name) {
    if(!strcmp(name,"inSampler_texturesetColor") || !strcmp(name,"inSampler_texturesetColorAlpha"))return KZSF_MAP_COLOR;
    const char *normal[]={"inSampler_texturesetNormal","inSampler_detailset_BNormal","inSampler_detailset_GNormal",
        "inSampler_detailset_RNormal","inSampler_dust_set1Normal"};
    for(unsigned i=0;i<sizeof(normal)/sizeof(*normal);i++)if(!strcmp(name,normal[i]))return KZSF_MAP_NORMAL;
    if(!strcmp(name,"inSampler_texturesetMaskReflectance"))return KZSF_MAP_MASK;
    const char *rough[]={"inSampler_texturesetRoughnessMask_AlphaAO","inSampler_detailset_BRoughness",
        "inSampler_detailset_GRoughness","inSampler_detailset_RRoughness","inSampler_dust_set1RoughnessMaskReflectance"};
    for(unsigned i=0;i<sizeof(rough)/sizeof(*rough);i++)if(!strcmp(name,rough[i]))return KZSF_MAP_ROUGHNESS;
    if(!strcmp(name,"inSampler_dust_set1Color"))return KZSF_MAP_WHITE;
    if(!strcmp(name,"inSampler_lit_sphere"))return KZSF_MAP_LIGHTING;
    return KZSF_MAP_UNKNOWN;
}
/* Visually confirmed CJ neutral surfaces: mask alpha zero suppresses the
 * inherited reflectance; packed blue stays full for the AO calculation.
 * These values apply to this observed weighted-body shader only. */
static const unsigned char kzsf_custom_material_neutral_rgba[4][4]={
    {128,128,255,255}, {0,0,0,0}, {128,0,255,255}, {255,255,255,255}
};
#endif
