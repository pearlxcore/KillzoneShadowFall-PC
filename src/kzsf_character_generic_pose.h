#ifndef KZSF_CHARACTER_GENERIC_POSE_H
#define KZSF_CHARACTER_GENERIC_POSE_H
#include "kzsf_character_bind_pose.h"
enum {KZSF_CHARACTER_GENERIC_MAX=320};
typedef struct {
    int from_count,to_count,matched;
    int16_t source[KZSF_CHARACTER_GENERIC_MAX],parent[KZSF_CHARACTER_GENERIC_MAX];
    unsigned char finger[KZSF_CHARACTER_GENERIC_MAX];
    unsigned char wrist[KZSF_CHARACTER_GENERIC_MAX];
    float correction[KZSF_CHARACTER_GENERIC_MAX][16],local[KZSF_CHARACTER_GENERIC_MAX][16];
} KzsfCharacterGenericPlan;
static inline int kzsf_character_finger_name(const char *name) {
    static const char *parts[]={"thumbOne","thumbTwo","thumbThree","indexZero","indexOne","indexTwo","indexThree",
        "middleZero","middleOne","middleTwo","middleThree","ringZero","ringOne","ringTwo","ringThree",
        "pinkyZero","pinkyOne","pinkyTwo","pinkyThree"};
    for(unsigned i=0;i<sizeof(parts)/sizeof(parts[0]);i++) {
        size_t n=strlen(parts[i]);
        if(!strncmp(name,parts[i],n) && (!strcmp(name+n,"_left") || !strcmp(name+n,"_right")))return 1;
    }
    return 0;
}
static inline int kzsf_character_generic_names(uintptr_t binding,int count,KzsfZoomRead read,void *opaque,
        char names[KZSF_CHARACTER_GENERIC_MAX][129],float inverse[KZSF_CHARACTER_GENERIC_MAX][16]) {
    int32_t n=0,matrices=0;uintptr_t text_array=0,data=0;
    if(count<1 || count>KZSF_CHARACTER_GENERIC_MAX || !kzsf_zoom_read(read,opaque,binding,0x10,&n,4) || n!=count ||
       !kzsf_zoom_read(read,opaque,binding,0x28,&matrices,4) || matrices!=count ||
       !kzsf_zoom_read(read,opaque,binding,0x18,&text_array,8) || !text_array ||
       !kzsf_zoom_read(read,opaque,binding,0x30,&data,8) || !data)return 0;
    for(int i=0;i<count;i++) {
        uintptr_t text=0;int32_t length=0;
        if(!kzsf_zoom_read(read,opaque,text_array,(size_t)i*8,&text,8) || !text ||
           !kzsf_zoom_read(read,opaque,text,8,&length,4) || length<1 || length>128 ||
           !kzsf_zoom_read(read,opaque,text,0x10,names[i],(size_t)length) || memchr(names[i],0,(size_t)length) ||
           !kzsf_zoom_read(read,opaque,data,(size_t)i*64,inverse[i],64))return 0;
        names[i][length]=0;
        for(int k=0;k<16;k++)if(!isfinite(inverse[i][k]))return 0;
        for(int j=0;j<i;j++)if(!strcmp(names[i],names[j]))return 0;
    }
    return 1;
}
static inline int kzsf_character_generic_plan(const KzsfCharacterMeshLeaf *from,const KzsfCharacterMeshLeaf *to,
        KzsfZoomRead read,void *opaque,KzsfCharacterGenericPlan *out) {
    int32_t a=0,b=0;uintptr_t array=0;
    if(!from || !to || !out || !kzsf_zoom_read(read,opaque,from->skeleton,0x30,&a,4) ||
       !kzsf_zoom_read(read,opaque,to->skeleton,0x30,&b,4) || a<78 || a>KZSF_CHARACTER_GENERIC_MAX || b<78 || b>KZSF_CHARACTER_GENERIC_MAX ||
       from->binding_count<78 || from->binding_count>KZSF_CHARACTER_GENERIC_MAX || to->binding_count!=b ||
       !kzsf_zoom_read(read,opaque,to->skeleton,0x38,&array,8) || !array)return 0;
    char from_names[KZSF_CHARACTER_GENERIC_MAX][129],to_names[KZSF_CHARACTER_GENERIC_MAX][129],from_joints[KZSF_CHARACTER_GENERIC_MAX][129];
    float from_inverse[KZSF_CHARACTER_GENERIC_MAX][16],to_inverse[KZSF_CHARACTER_GENERIC_MAX][16],bind[KZSF_CHARACTER_GENERIC_MAX][16];
    if(!kzsf_character_generic_names(from->bindings,from->binding_count,read,opaque,from_names,from_inverse) ||
       !kzsf_character_generic_names(to->bindings,b,read,opaque,to_names,to_inverse))return 0;
    for(int i=0;i<a;i++) {
        if(!kzsf_character_joint_name(from->skeleton,i,read,opaque,from_joints[i]))return 0;
        for(int j=0;j<i;j++)if(!strcmp(from_joints[i],from_joints[j]))return 0;
    }
    KzsfCharacterGenericPlan plan={.from_count=a,.to_count=b};
    for(int i=0;i<b;i++) {
        char name[129];int source=-1,source_binding=-1,target_binding=-1;
        if(!kzsf_character_joint_name(to->skeleton,i,read,opaque,name) ||
           !kzsf_zoom_read(read,opaque,array,(size_t)i*24+0x10,&plan.parent[i],2) ||
           plan.parent[i]<-1 || plan.parent[i]>=i)return 0;
        for(int j=0;j<i;j++){char earlier[129];if(!kzsf_character_joint_name(to->skeleton,j,read,opaque,earlier) || !strcmp(name,earlier))return 0;}
        for(int j=0;j<a;j++) {
            if(!strcmp(name,from_joints[j]))source=j;
        }
        for(int j=0;j<from->binding_count;j++)if(!strcmp(name,from_names[j]))source_binding=j;
        /* A native pose can retain a larger render layout while its animated
         * worlds still belong to the player's bind space. Extra procedural
         * joints without a binding in that space must use the parent path. */
        if(source_binding<0)source=-1;
        for(int j=0;j<b;j++)if(!strcmp(name,to_names[j]))target_binding=j;
        if(target_binding<0 || !kzsf_character_matrix_inverse(to_inverse[target_binding],bind[i]))return 0;
        plan.source[i]=(int16_t)source;
        if(source>=0) {
            if(source_binding<0 || !kzsf_character_matrix_multiply(from_inverse[source_binding],bind[i],plan.correction[i]))return 0;
            plan.matched++;
        }
        if(plan.parent[i]<0)memcpy(plan.local[i],bind[i],64);
        else {float inverse[16];if(!kzsf_character_matrix_inverse(bind[plan.parent[i]],inverse) ||
                !kzsf_character_matrix_multiply(inverse,bind[i],plan.local[i]))return 0;}
        if(plan.parent[i]>=0 && kzsf_character_finger_name(name)) {
            char parent_name[129];
            if(!kzsf_character_joint_name(to->skeleton,plan.parent[i],read,opaque,parent_name))return 0;
            const char *side=strrchr(name,'_'),*parent_side=strrchr(parent_name,'_');
            plan.finger[i]=(unsigned char)(side && parent_side && !strcmp(side,parent_side) &&
                (kzsf_character_finger_name(parent_name) || !strcmp(parent_name,"wrist_left") || !strcmp(parent_name,"wrist_right")));
        }
    }
    if(plan.matched<78)return 0;
    *out=plan;return 1;
}
static inline int kzsf_character_generic_worlds(const KzsfCharacterGenericPlan *plan,const float input[][16],float output[][16]) {
    if(!plan || plan->from_count<78 || plan->from_count>KZSF_CHARACTER_GENERIC_MAX || plan->to_count<78 || plan->to_count>KZSF_CHARACTER_GENERIC_MAX)return 0;
    float result[KZSF_CHARACTER_GENERIC_MAX][16];
    for(int i=0;i<plan->to_count;i++) {
        int source=plan->source[i],parent=plan->parent[i],finite=1;
        if(source < -1 || source>=plan->from_count || parent < -1 || parent>=i)return 0;
        if(source>=0)for(int k=0;k<16;k++)if(!isfinite(input[source][k]))finite=0;
        if(source>=0 && finite) {
            if(!kzsf_character_matrix_multiply(input[source],plan->correction[i],result[i]))return 0;
        } else if(parent<0) {
            memcpy(result[i],plan->local[i],64);
            for(int k=0;k<16;k++)if(!isfinite(result[i][k]))return 0;
        } else if(!kzsf_character_matrix_multiply(result[parent],plan->local[i],result[i]))return 0;
    }
    memcpy(output,result,(size_t)plan->to_count*64);return 1;
}
/* The native 78 animated worlds remain usable after an imported render-rig
 * rebuild. Its procedural helpers do not: captures contain both NaNs and
 * finite helpers displaced from their supporting limb. Finiteness alone
 * cannot authorize copying those results back to the player's 98-joint rig.
 * Rebuild the 20 player helpers from their validated rest-local hierarchy.
 * Call only for an imported source layout; preserve native player helpers. */
static inline int kzsf_character_generic_rebuild_player_helpers(KzsfCharacterGenericPlan *plan) {
    if(!plan || plan->to_count!=98 || plan->from_count<78 || plan->from_count>KZSF_CHARACTER_GENERIC_MAX)return 0;
    for(int i=78;i<98;i++) {
        if(plan->parent[i]<0 || plan->parent[i]>=i)return 0;
    }
    for(int i=78;i<98;i++)plan->source[i]=-1;
    return 1;
}
/* Independent world-space bind conversions can disconnect finger segments
 * as they curl. Keep their converted rotations but anchor each joint at the
 * corrected parent's target bind offset, preserving finger segment lengths. */
static inline int kzsf_character_generic_draw_worlds(const KzsfCharacterGenericPlan *plan,
        const float input[][16],float output[][16]) {
    float result[KZSF_CHARACTER_GENERIC_MAX][16];
    if(!kzsf_character_generic_worlds(plan,input,result))return 0;
    for(int i=0;i<plan->to_count;i++)if(plan->finger[i] || plan->wrist[i]) {
        int parent=plan->parent[i];if(parent<0 || parent>=i)return 0;
        for(int row=0;row<3;row++) {
            double position=result[parent][12+row];
            for(int k=0;k<3;k++)position+=(double)result[parent][k*4+row]*plan->local[i][12+k];
            result[i][12+row]=(float)position;if(!isfinite(result[i][12+row]))return 0;
        }
    }
    memcpy(output,result,(size_t)plan->to_count*64);return 1;
}
/* Echo's differently proportioned arm bind space needs the same connection
 * constraint at the wrist. Opt in at selection; other models stay unchanged. */
static inline int kzsf_character_generic_anchor_wrists(KzsfCharacterGenericPlan *plan,uintptr_t skeleton,
        KzsfZoomRead read,void *opaque) {
    int found[2]={-1,-1};int32_t count=0;
    if(!plan || !kzsf_zoom_read(read,opaque,skeleton,0x30,&count,4) || count!=plan->to_count ||
       count<78 || count>KZSF_CHARACTER_GENERIC_MAX)return 0;
    for(int i=0;i<count;i++) {
        char name[129],parent_name[129];
        if(!kzsf_character_joint_name(skeleton,i,read,opaque,name))return 0;
        int side=!strcmp(name,"wrist_left") ? 0 : !strcmp(name,"wrist_right") ? 1 : -1;
        if(side<0)continue;
        int parent=plan->parent[i];
        if(found[side]>=0 || parent<0 || parent>=i ||
           !kzsf_character_joint_name(skeleton,parent,read,opaque,parent_name) ||
           strcmp(parent_name,side ? "lowerArm_right" : "lowerArm_left"))return 0;
        found[side]=i;
    }
    if(found[0]<0 || found[1]<0)return 0;
    plan->wrist[found[0]]=plan->wrist[found[1]]=1;return 1;
}
static inline int kzsf_character_generic_read_worlds(uintptr_t pose,int count,KzsfZoomRead read,void *opaque,float output[][16]) {
    uintptr_t start=0,end=0;
    return count>=78 && count<=KZSF_CHARACTER_GENERIC_MAX && kzsf_zoom_read(read,opaque,pose,0x40,&start,8) && start &&
        kzsf_zoom_read(read,opaque,pose,0x48,&end,8) && end>=start && end-start>=(size_t)count*64 &&
        kzsf_zoom_read(read,opaque,start,0,output,(size_t)count*64);
}
static inline int kzsf_character_generic_write_worlds(uintptr_t pose,int count,KzsfZoomRead read,
        KzsfCharacterPoseWrite write,void *opaque,const float input[][16]) {
    uintptr_t start=0,end=0;
    return count>=78 && count<=KZSF_CHARACTER_GENERIC_MAX && kzsf_zoom_read(read,opaque,pose,0x40,&start,8) && start &&
        kzsf_zoom_read(read,opaque,pose,0x48,&end,8) && end>=start && end-start>=(size_t)count*64 &&
        write(opaque,start,input,(size_t)count*64);
}
static inline int kzsf_character_generic_map_prepare(const KzsfCharacterNativeMap *map,uintptr_t pose,
        int source_count,int target_count,KzsfZoomRead read,void *opaque,
        KzsfCharacterJointMap joints[KZSF_CHARACTER_GENERIC_MAX],int32_t channels[128],KzsfCharacterNativeMap *view) {
    uintptr_t scalars=0,end=0;
    if(source_count<78 || source_count>KZSF_CHARACTER_GENERIC_MAX || target_count<78 || target_count>KZSF_CHARACTER_GENERIC_MAX ||
       map->joints.count!=target_count || map->joints.capacity<target_count || !map->joints.data ||
       map->channels.count<0 || map->channels.count>128 || map->channels.capacity<map->channels.count ||
       !kzsf_zoom_read(read,opaque,map->joints.data,0,joints,(size_t)target_count*8) ||
       !kzsf_zoom_read(read,opaque,pose,0x50,&scalars,8) || !kzsf_zoom_read(read,opaque,pose,0x58,&end,8) ||
       end<scalars || (end-scalars)%4 || end-scalars>4096*4)return 0;
    for(int i=0;i<target_count;i++) {
        if(joints[i].source < -1 || joints[i].source>=source_count)return 0;
        joints[i].parent_differs=joints[i].source>=0;
    }
    if(map->channels.count && (!map->channels.data || !kzsf_zoom_read(read,opaque,map->channels.data,0,channels,(size_t)map->channels.count*4)))return 0;
    for(int i=0;i<map->channels.count;i++)if(channels[i]<-1 || (channels[i]>=0 && (!scalars || (uintptr_t)channels[i]>=(end-scalars)/4)))return 0;
    *view=*map;view->joints.data=(uintptr_t)joints;view->channels.data=(uintptr_t)channels;return 1;
}
#endif
