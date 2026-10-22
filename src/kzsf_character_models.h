#ifndef KZSF_CHARACTER_MODELS_H
#define KZSF_CHARACTER_MODELS_H
#include <string.h>
#include <stdint.h>
enum { KZSF_CHARACTER_MODEL_CJ=17, KZSF_CHARACTER_MODEL_COUNT=18 };
/* Preserve stable IDs and bundle metadata while the two Lucas outfit paths
 * still render the default suit. Re-enable after visual validation. */
static inline int kzsf_character_model_selectable(int model) {
    return model>=0 && model<KZSF_CHARACTER_MODEL_COUNT && model!=13 && model!=16;
}
static inline uint64_t kzsf_character_model_mask(void) {
    return ((UINT64_C(1)<<KZSF_CHARACTER_MODEL_COUNT)-1)&~((UINT64_C(1)<<13)|(UINT64_C(1)<<16));
}
typedef struct {const char *name,*label,*bundle;} KzsfCharacterModelDefinition;
/* One row per supported model. Chapter discovery adds rows here, with the
 * native SoldierResource name and its on-disk bundle; no chapter-specific
 * whitelist or loader implementation is needed for new entries. */
static inline const KzsfCharacterModelDefinition *kzsf_character_model_definition(int model) {
    static const KzsfCharacterModelDefinition models[]={
        {"ISA_Lamb","Default player",""},
        {"VSA_Engineer","VSA Engineer","assets_description.characters.vsa_engineer_vsa_engineer_character"},
        {"HGH_Commando","HGH Commando","assets_description.characters.hgh_commando_hgh_commando_character"},
        {"HGH_Security_Trooper","HGH Security Trooper (experimental)","assets_description.characters.hgh_security_trooper_hgh_security_trooper_character"},
        {"HGH_Sniper","HGH Sniper (experimental)","assets_description.characters.hgh_sniper_hgh_sniper_character"},
        {"VSA_Special_Forces","VSA Special Forces (experimental)","assets_description.characters.vsa_special_forces_vsa_special_forces_character"},
        {"VSA_Infantry_Trooper","VSA Infantry Trooper (experimental)","assets_description.characters.vsa_infantry_trooper_vsa_infantry_trooper_character"},
        {"HGH_Assault_Trooper","HGH Assault Trooper (experimental)","assets_description.characters.hgh_assault_trooper_hgh_assault_trooper_character"},
        {"HGH_Medic","HGH Medic (experimental)","assets_description.characters.hgh_medic_hgh_medic_character"},
        {"ISA_Echo","Echo (experimental)","assets_description.characters.isa_echo_isa_echo_character"},
        {"VSA_Security_Trooper","VSA Security Trooper (experimental)","assets_description.characters.vsa_security_trooper_vsa_security_trooper_character"},
        {"VSA_Sinclair","Sinclair (experimental)","assets_description.characters.vsa_sinclair_vsa_sinclair_character"},
        {"ISA_Lamb_Civilian_Disguise","Lucas - civilian disguise (experimental)","assets_description.characters.isa_lamb_isa_lamb_civilian_disguise_character"},
        {"ISA_Lamb_Echo","Lucas - Echo outfit (experimental)","assets_description.characters.isa_lamb_isa_lamb_echo_character"},
        {"ISA_Lamb_Helghan","Lucas - Helghan outfit (experimental)","assets_description.characters.isa_lamb_isa_lamb_helghan_character"},
        {"ISA_Lamb_Prisoner","Lucas - prisoner (experimental)","assets_description.characters.isa_lamb_isa_lamb_prisoner_character"},
        {"ISA_Lamb_SpaceSuit","Lucas - spacesuit (experimental)","assets_description.characters.isa_lamb_isa_lamb_spacesuit_character"},
        {"Custom_CJ","CJ (custom)",""}};
#ifdef __cplusplus
    static_assert(KZSF_CHARACTER_MODEL_COUNT<64,"model availability mask capacity");
    static_assert(sizeof(models)/sizeof(*models)==KZSF_CHARACTER_MODEL_COUNT,"model metadata count");
#else
    _Static_assert(KZSF_CHARACTER_MODEL_COUNT<64,"model availability mask capacity");
    _Static_assert(sizeof(models)/sizeof(*models)==KZSF_CHARACTER_MODEL_COUNT,"model metadata count");
#endif
    return &models[model>=0 && model<KZSF_CHARACTER_MODEL_COUNT ? model : 0];
}
static inline const char *kzsf_character_model_name(int model) {
    return model>=0 && model<KZSF_CHARACTER_MODEL_COUNT ? kzsf_character_model_definition(model)->name : "";
}
static inline int kzsf_character_model_index(const char *name) {
    for(int i=0;i<KZSF_CHARACTER_MODEL_COUNT;i++)if(!strcmp(name,kzsf_character_model_name(i)))return i;
    return -1;
}
/* Bundle identities are independent of chapters. New supported entries must
 * declare their bundle here so they use the same resident-loading path. */
static inline const char *kzsf_character_model_bundle(int model) {
    return model>0 && model<KZSF_CHARACTER_MODEL_COUNT ? kzsf_character_model_definition(model)->bundle : "";
}
static inline int kzsf_character_next_loaded(int current,int direction,uint64_t mask) {
    if(current<0 || current>=KZSF_CHARACTER_MODEL_COUNT)current=0;
    if(direction!=1 && direction!=-1)return current;
    mask=(mask&kzsf_character_model_mask())|1;
    for(int step=1;step<=KZSF_CHARACTER_MODEL_COUNT;step++) {
        int next=(current+direction*step+KZSF_CHARACTER_MODEL_COUNT)%KZSF_CHARACTER_MODEL_COUNT;
        if(mask&(UINT64_C(1)<<next))return next;
    }
    return current;
}
typedef struct {int f7,left,right,used;} KzsfCharacterModelKeys;
/* +/-1 cycles,2 toggles on release. A chord must never also toggle default. */
static inline int kzsf_character_model_keys(KzsfCharacterModelKeys *state,int active,int f7,int left,int right) {
    if(!active){state->f7=state->left=state->right=state->used=0;return 0;}
    int action=0;
    if(f7) {
        int newly_pressed=!state->f7;
        if(newly_pressed)state->used=0;
        if(right && (newly_pressed || !state->right)){action=1;state->used=1;}
        else if(left && (newly_pressed || !state->left)){action=-1;state->used=1;}
    } else if(state->f7 && !state->used)action=2;
    state->f7=f7;state->left=left;state->right=right;return action;
}
#endif
