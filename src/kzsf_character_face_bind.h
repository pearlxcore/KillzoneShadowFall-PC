#ifndef KZSF_CHARACTER_FACE_BIND_H
#define KZSF_CHARACTER_FACE_BIND_H
#include "kzsf_character_face.h"
#include "kzsf_character_generic_pose.h"

/* The body draw adapter converts the player's animated worlds into the
 * selected mesh's bind space. Apply that same conversion before the native
 * face correction, resolving animation78 indices by name into both layouts. */
static inline int kzsf_character_face_bind_variant(uintptr_t variant,uintptr_t actual,
        const KzsfCharacterMeshLeaf *original,const KzsfCharacterMeshLeaf *target,
        KzsfZoomRead read,void *opaque,unsigned char prepared[0xf0],int32_t mapped[512],
        float corrected[512][16],KzsfCharacterGenericPlan *plan) {
    if(!original || !target || !corrected || !plan ||
       !kzsf_character_face_reindex(variant,actual,read,opaque,prepared,mapped))return 0;
    KzsfCharacterMeshLeaf source=*original;source.skeleton=actual;
    if(!kzsf_character_generic_plan(&source,target,read,opaque,plan))return 0;
    uintptr_t expected=0,indices=0,matrices=0;int32_t count=0,matrix_count=0;
    if(!kzsf_zoom_read(read,opaque,variant,0x60,&expected,8) ||
       !kzsf_zoom_read(read,opaque,variant,0xc0,&indices,8))return 0;
    memcpy(&count,prepared+0xb8,4);memcpy(&matrix_count,prepared+0xd0,4);memcpy(&matrices,prepared+0xd8,8);
    if(matrix_count<1 || matrix_count>512 || !matrices ||
       !kzsf_zoom_read(read,opaque,matrices,0,corrected,(size_t)matrix_count*64))return 0;
    int cursor=0;
    for(int i=0;i<count;i++) {
        int32_t joint=0;char name[129];int target_joint=-1;
        if(!kzsf_zoom_read(read,opaque,indices,(size_t)i*4,&joint,4))return 0;
        if(joint==-1)continue;
        if(cursor>=matrix_count || !kzsf_character_joint_name(expected,joint,read,opaque,name))return 0;
        for(int j=0;j<plan->to_count;j++) {
            char candidate[129];
            if(!kzsf_character_joint_name(target->skeleton,j,read,opaque,candidate))return 0;
            if(!strcmp(name,candidate)){if(target_joint>=0)return 0;target_joint=j;}
        }
        if(target_joint<0 || plan->source[target_joint]!=mapped[i] ||
           !kzsf_character_matrix_multiply(plan->correction[target_joint],corrected[cursor],corrected[cursor]))return 0;
        ++cursor;
    }
    if(!cursor)return 0;
    uintptr_t address=(uintptr_t)corrected;
    memcpy(prepared+0xd8,&address,8);return 1;
}
#endif
