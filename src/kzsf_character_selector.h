#ifndef KZSF_CHARACTER_SELECTOR_H
#define KZSF_CHARACTER_SELECTOR_H
#include "kzsf_character_swap.h"
#include "kzsf_character_rig_audit.h"
enum { KZSF_CHARACTER_SELECTION_LIMIT=256 };
typedef struct { uintptr_t part;char name[128];int32_t variant; } KzsfCharacterChoice;
/* Names/variants provide stable ordering across registry enumeration orders.
 * All pointers live only for the current engine callback. */
static inline int kzsf_character_choice_add(KzsfCharacterChoice *choices,int32_t *count,
        uintptr_t part,const char *name,int32_t variant) {
    if(!choices || !count || *count<0 || *count>KZSF_CHARACTER_SELECTION_LIMIT || !part || !name || strlen(name)>=128)return 0;
    for(int32_t i=0;i<*count;i++) {
        /* Distinct SoldierResources can share a body but carry different
         * face/appearance data. Collapse repeated variants only within the
         * same named resource, never erase another selectable identity. */
        if(choices[i].part==part && !strcmp(choices[i].name,name))return 1;
        if(!strcmp(choices[i].name,name) && choices[i].variant==variant)return 0;
    }
    if(*count==KZSF_CHARACTER_SELECTION_LIMIT)return 0;
    int32_t slot=*count;
    while(slot>0) {
        int cmp=strcmp(choices[slot-1].name,name);
        if(cmp<0 || (!cmp && choices[slot-1].variant<=variant))break;
        choices[slot]=choices[slot-1];slot--;
    }
    choices[slot].part=part;strcpy(choices[slot].name,name);choices[slot].variant=variant;(*count)++;
    return 1;
}
static inline int32_t kzsf_character_choice_next(int32_t current,int32_t count,const unsigned char *eligible) {
    if(!eligible || count<0 || count>KZSF_CHARACTER_SELECTION_LIMIT || current < -1 || current>=count)return -1;
    for(int32_t i=current+1;i<count;i++)if(eligible[i])return i;
    return -1; /* default body is the final entry in each cycle */
}
#endif
