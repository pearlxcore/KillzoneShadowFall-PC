#ifndef KZSF_CHARACTER_BIND_POSE_H
#define KZSF_CHARACTER_BIND_POSE_H
#include "kzsf_character_rig_audit.h"
#include "kzsf_character_retarget.h"
#include <math.h>

/* Column-major Mat44. Converts source animated worlds to the target rig's bind
 * space before skinning; no resource writes. */
typedef struct { int16_t parent[103]; int16_t source[103]; unsigned char missing[103]; float correction[103][16],local[103][16]; } KzsfCharacterBindPlan;
typedef int (*KzsfCharacterPoseWrite)(void *,uintptr_t,const void *,size_t);
static inline int kzsf_character_matrix_multiply(const float a[16],const float b[16],float out[16]) {
    float r[16];
    for(int c=0;c<4;c++)for(int row=0;row<4;row++) {
        double v=0;for(int k=0;k<4;k++)v+=(double)a[k*4+row]*b[c*4+k];
        r[c*4+row]=(float)v;if(!isfinite(r[c*4+row]))return 0;
    }
    memcpy(out,r,sizeof(r));return 1;
}
static inline int kzsf_character_matrix_inverse(const float in[16],float out[16]) {
    double a[4][8];
    for(int row=0;row<4;row++)for(int c=0;c<8;c++) {
        a[row][c]=c<4 ? in[c*4+row] : (double)(c-4==row);
        if(!isfinite(a[row][c]))return 0;
    }
    for(int c=0;c<4;c++) {
        int pivot=c;for(int row=c+1;row<4;row++)if(fabs(a[row][c])>fabs(a[pivot][c]))pivot=row;
        if(fabs(a[pivot][c])<1e-10)return 0;
        for(int k=0;k<8;k++){double t=a[c][k];a[c][k]=a[pivot][k];a[pivot][k]=t;}
        double scale=a[c][c];for(int k=0;k<8;k++)a[c][k]/=scale;
        for(int row=0;row<4;row++)if(row!=c) {
            double factor=a[row][c];for(int k=0;k<8;k++)a[row][k]-=factor*a[c][k];
        }
    }
    for(int row=0;row<4;row++)for(int c=0;c<4;c++) {
        out[c*4+row]=(float)a[row][c+4];if(!isfinite(out[c*4+row]))return 0;
    }
    return 1;
}
static inline int kzsf_character_bind_plan(const KzsfCharacterMeshLeaf *source_leaf,const KzsfCharacterMeshLeaf *target_leaf,
        const KzsfCharacterJointMap joints[103],KzsfZoomRead read,void *opaque,KzsfCharacterBindPlan *out) {
    uintptr_t source_names=0,source_matrices=0,source_array=0,target_names=0,target_matrices=0,target_array=0;
    int32_t sc=0,tc=0,smc=0,tmc=0,sbones=0,tbones=0;
    if(!source_leaf || !target_leaf || !out || source_leaf->binding_count!=98 || target_leaf->binding_count!=103 ||
       !kzsf_zoom_read(read,opaque,source_leaf->skeleton,0x30,&sbones,4) || sbones!=98 ||
       !kzsf_zoom_read(read,opaque,target_leaf->skeleton,0x30,&tbones,4) || tbones!=103 ||
       !kzsf_zoom_read(read,opaque,source_leaf->skeleton,0x38,&source_array,8) || !source_array ||
       !kzsf_zoom_read(read,opaque,target_leaf->skeleton,0x38,&target_array,8) || !target_array ||
       !kzsf_zoom_read(read,opaque,source_leaf->bindings,0x10,&sc,4) || sc!=98 ||
       !kzsf_zoom_read(read,opaque,source_leaf->bindings,0x18,&source_names,8) || !source_names ||
       !kzsf_zoom_read(read,opaque,source_leaf->bindings,0x28,&smc,4) || smc!=98 ||
       !kzsf_zoom_read(read,opaque,source_leaf->bindings,0x30,&source_matrices,8) || !source_matrices ||
       !kzsf_zoom_read(read,opaque,target_leaf->bindings,0x10,&tc,4) || tc!=103 ||
       !kzsf_zoom_read(read,opaque,target_leaf->bindings,0x18,&target_names,8) || !target_names ||
       !kzsf_zoom_read(read,opaque,target_leaf->bindings,0x28,&tmc,4) || tmc!=103 ||
       !kzsf_zoom_read(read,opaque,target_leaf->bindings,0x30,&target_matrices,8) || !target_matrices)return 0;
    char source_binding_names[98][129],target_binding_names[103][129];
    float source_inv_bind[98][16],target_bind[103][16];
    for(int i=0;i<98;i++) {
        uintptr_t text=0;int32_t length=0;
        if(!kzsf_zoom_read(read,opaque,source_names,(size_t)i*8,&text,8) || !text ||
           !kzsf_zoom_read(read,opaque,text,8,&length,4) || length<=0 || length>128 ||
           !kzsf_zoom_read(read,opaque,text,0x10,source_binding_names[i],(size_t)length))return 0;
        source_binding_names[i][length]=0;if(memchr(source_binding_names[i],0,(size_t)length))return 0;
        for(int j=0;j<i;j++)if(!strcmp(source_binding_names[i],source_binding_names[j]))return 0;
        if(!kzsf_zoom_read(read,opaque,source_matrices,(size_t)i*64,source_inv_bind[i],64))return 0;
        for(int k=0;k<16;k++)if(!isfinite(source_inv_bind[i][k]))return 0;
    }
    KzsfCharacterBindPlan plan={0};int missing=0;unsigned char seen[98]={0};
    for(int i=0;i<103;i++) {
        char target_name[129];
        if(!kzsf_character_joint_name(target_leaf->skeleton,i,read,opaque,target_name) ||
           !kzsf_zoom_read(read,opaque,target_array,(size_t)i*0x18+0x10,&plan.parent[i],2) ||
           plan.parent[i]<-1 || plan.parent[i]>=i)return 0;
        uintptr_t text=0;int32_t length=0;int target_binding=-1;
        if(!kzsf_zoom_read(read,opaque,target_names,(size_t)i*8,&text,8) || !text ||
           !kzsf_zoom_read(read,opaque,text,8,&length,4) || length<=0 || length>128 ||
           !kzsf_zoom_read(read,opaque,text,0x10,target_binding_names[i],(size_t)length))return 0;
        target_binding_names[i][length]=0;
        if(memchr(target_binding_names[i],0,(size_t)length))return 0;
        for(int j=0;j<i;j++)if(!strcmp(target_binding_names[i],target_binding_names[j]))return 0;
        for(int j=0;j<103;j++) {
            uintptr_t binding_text=0;int32_t binding_length=0;char binding_name[129];
            if(!kzsf_zoom_read(read,opaque,target_names,(size_t)j*8,&binding_text,8) || !binding_text ||
               !kzsf_zoom_read(read,opaque,binding_text,8,&binding_length,4) || binding_length<=0 || binding_length>128 ||
               !kzsf_zoom_read(read,opaque,binding_text,0x10,binding_name,(size_t)binding_length))return 0;
            binding_name[binding_length]=0;
            if(!strcmp(target_name,binding_name)){target_binding=j;break;}
        }
        if(target_binding<0 || strcmp(target_name,target_binding_names[i])) {
            /* Binding order may differ from skeleton order; the per-joint name
             * is validated against the matching binding slot below instead. */
            if(target_binding<0)return 0;
        }
        float target_inv_bind[16];
        if(!kzsf_zoom_read(read,opaque,target_matrices,(size_t)target_binding*64,target_inv_bind,64) ||
           !kzsf_character_matrix_inverse(target_inv_bind,target_bind[i]))return 0;
        int source=joints[i].source;
        plan.source[i]=(int16_t)source;
        if(source==-1){plan.missing[i]=1;missing++;}
        else {
            if(source<0 || source>=98 || seen[source]++)return 0;
            int source_binding=-1;
            for(int j=0;j<98;j++)if(!strcmp(target_name,source_binding_names[j])){source_binding=j;break;}
            if(source_binding<0 || !kzsf_character_matrix_multiply(source_inv_bind[source_binding],target_bind[i],plan.correction[i]))return 0;
        }
        /* InverseBind(parent) * Bind(child) is its rest local offset. */
        if(plan.parent[i]<0)memcpy(plan.local[i],target_bind[i],64);
        else {
            float parent_inverse[16];
            if(!kzsf_character_matrix_inverse(target_bind[plan.parent[i]],parent_inverse) ||
               !kzsf_character_matrix_multiply(parent_inverse,target_bind[i],plan.local[i]))return 0;
        }
    }
    if(missing!=5)return 0;
    *out=plan;return 1;
}
static inline int kzsf_character_bind_worlds(const KzsfCharacterBindPlan *plan,const float source_world[98][16],float world[103][16]) {
    for(int i=0;i<103;i++) {
        if(plan->parent[i]<-1 || plan->parent[i]>=i)return 0;
        if(!plan->missing[i]) {
            int source=plan->source[i];if(source<0 || source>=98 ||
               !kzsf_character_matrix_multiply(source_world[source],plan->correction[i],world[i]))return 0;
            continue;
        }
        if(plan->parent[i]<0)memcpy(world[i],plan->local[i],64);
        else if(!kzsf_character_matrix_multiply(world[plan->parent[i]],plan->local[i],world[i]))return 0;
    }
    return 1;
}
/* Reverse native103 transition. Preserve all native draw records rather than
 * omitting a body that may provide renderer state.
 * Invert exactly the named98->103 correction, preserving the source ordering. */
typedef struct {int16_t target[98];float correction[98][16];} KzsfCharacterReverseBindPlan;
/* The captured native103 pose has five invalid leg-helper rotations at98..102.
 * Keep every finite native joint; rebuild only those helper worlds from the
 * target rest-local transform and an already validated parent. */
static inline int kzsf_character_repair103_worlds(const KzsfCharacterBindPlan *plan,
        const float input[103][16],float output[103][16],int *repaired) {
    if(!plan || !input || !output || !repaired)return 0;
    float result[103][16];int fixed=0;
    for(int i=0;i<103;i++) {
        int parent=plan->parent[i],finite=1;
        if(parent < -1 || parent>=i)return 0;
        for(int k=0;k<16;k++)if(!isfinite(input[i][k]))finite=0;
        if(finite){memcpy(result[i],input[i],64);continue;}
        if(i<98 || parent<0 || !kzsf_character_matrix_multiply(result[parent],plan->local[i],result[i]))return 0;
        fixed++;
    }
    memcpy(output,result,sizeof(result));*repaired=fixed;return 1;
}
static inline int kzsf_character_repair103_pose(const KzsfCharacterBindPlan *plan,uintptr_t source,uintptr_t destination,
        KzsfZoomRead read,KzsfCharacterPoseWrite write,void *opaque,int *repaired) {
    uintptr_t world=0,end=0,out=0,out_end=0;float input[103][16],output[103][16];
    if(!write || !kzsf_zoom_read(read,opaque,source,0x40,&world,8) || !world ||
       !kzsf_zoom_read(read,opaque,source,0x48,&end,8) || end<world || end-world<sizeof(input) ||
       !kzsf_zoom_read(read,opaque,world,0,input,sizeof(input)) ||
       !kzsf_zoom_read(read,opaque,destination,0x40,&out,8) || !out ||
       !kzsf_zoom_read(read,opaque,destination,0x48,&out_end,8) || out_end<out || out_end-out<sizeof(output) ||
       !kzsf_character_repair103_worlds(plan,input,output,repaired))return 0;
    return write(opaque,out,output,sizeof(output));
}
static inline int kzsf_character_reverse_bind_plan(const KzsfCharacterBindPlan *forward,KzsfCharacterReverseBindPlan *out) {
    if(!forward || !out)return 0;
    KzsfCharacterReverseBindPlan plan={0};unsigned char seen[98]={0};int mapped=0;
    for(int i=0;i<103;i++) {
        if(forward->missing[i])continue;
        int source=forward->source[i];
        if(source<0 || source>=98 || seen[source]++ ||
           !kzsf_character_matrix_inverse(forward->correction[i],plan.correction[source]))return 0;
        plan.target[source]=(int16_t)i;mapped++;
    }
    if(mapped!=98)return 0;
    *out=plan;return 1;
}
static inline int kzsf_character_reverse_bind_worlds(const KzsfCharacterReverseBindPlan *plan,
        const float target_world[103][16],float source_world[98][16]) {
    if(!plan || !target_world || !source_world)return 0;
    float result[98][16];
    for(int i=0;i<98;i++) {
        int target=plan->target[i];
        if(target<0 || target>=103 ||
           !kzsf_character_matrix_multiply(target_world[target],plan->correction[i],result[i]))return 0;
    }
    memcpy(source_world,result,sizeof(result));return 1;
}
/* Only the captured103->98 body map is supported. Native allocation supplies
 * the scratch pose's local/scalar storage; force world reconstruction so it
 * cannot index the source local array using destination joint ordering. */
static inline int kzsf_character_reverse_map_prepare(const KzsfCharacterNativeMap *map,uintptr_t source,
        KzsfZoomRead read,void *opaque,KzsfCharacterJointMap joints[98],int32_t channels[4],KzsfCharacterNativeMap *view) {
    uintptr_t world=0,end=0,scalars=0,scalar_end=0;
    if(!map || !view || map->joints.count!=98 || map->joints.capacity<98 || !map->joints.data ||
       map->channels.count!=4 || map->channels.capacity<4 || !map->channels.data ||
       !kzsf_zoom_read(read,opaque,source,0x40,&world,8) || !world ||
       !kzsf_zoom_read(read,opaque,source,0x48,&end,8) || end<world || end-world<103*64 ||
       !kzsf_zoom_read(read,opaque,source,0x50,&scalars,8) ||
       !kzsf_zoom_read(read,opaque,source,0x58,&scalar_end,8) || scalar_end<scalars ||
       (scalar_end-scalars)%4 || scalar_end-scalars>4096*4 ||
       !kzsf_zoom_read(read,opaque,map->joints.data,0,joints,98*8) ||
       !kzsf_zoom_read(read,opaque,map->channels.data,0,channels,4*4))return 0;
    unsigned char seen[103]={0};
    for(int i=0;i<98;i++) {
        int index=joints[i].source;
        if(index<0 || index>=103 || seen[index]++)return 0;
        joints[i].parent_differs=1;
    }
    for(int i=0;i<4;i++)if(channels[i]<-1 || (channels[i]>=0 &&
            (!scalars || (uintptr_t)channels[i]>=(scalar_end-scalars)/4)))return 0;
    *view=*map;view->joints.data=(uintptr_t)joints;view->channels.data=(uintptr_t)channels;return 1;
}
static inline int kzsf_character_reverse_bind_pose(const KzsfCharacterReverseBindPlan *plan,uintptr_t source,uintptr_t destination,
        KzsfZoomRead read,KzsfCharacterPoseWrite write,void *opaque) {
    uintptr_t world=0,end=0,out=0,out_end=0;float target_world[103][16],source_world[98][16];
    if(!plan || !write || !kzsf_zoom_read(read,opaque,source,0x40,&world,8) || !world ||
       !kzsf_zoom_read(read,opaque,source,0x48,&end,8) || end<world || end-world<sizeof(target_world) ||
       !kzsf_zoom_read(read,opaque,world,0,target_world,sizeof(target_world)) ||
       !kzsf_zoom_read(read,opaque,destination,0x40,&out,8) || !out ||
       !kzsf_zoom_read(read,opaque,destination,0x48,&out_end,8) || out_end<out || out_end-out<sizeof(source_world) ||
       !kzsf_character_reverse_bind_worlds(plan,target_world,source_world))return 0;
    return write(opaque,out,source_world,sizeof(source_world));
}
static inline int kzsf_character_bind_pose(const KzsfCharacterBindPlan *plan,uintptr_t source,uintptr_t current,uintptr_t previous,
        KzsfZoomRead read,KzsfCharacterPoseWrite write,void *opaque) {
    uintptr_t sw=0,swe=0,a=0,ae=0,b=0,be=0;float source_world[98][16],world[103][16];
    if(!plan || !write || !kzsf_zoom_read(read,opaque,source,0x40,&sw,8) || !sw ||
       !kzsf_zoom_read(read,opaque,source,0x48,&swe,8) || swe<sw || swe-sw<sizeof(source_world) ||
       !kzsf_zoom_read(read,opaque,sw,0,source_world,sizeof(source_world)) ||
       !kzsf_zoom_read(read,opaque,current,0x40,&a,8) || !a ||
       !kzsf_zoom_read(read,opaque,current,0x48,&ae,8) || ae<a || ae-a<sizeof(world) ||
       !kzsf_zoom_read(read,opaque,previous,0x40,&b,8) || !b ||
       !kzsf_zoom_read(read,opaque,previous,0x48,&be,8) || be<b || be-b<sizeof(world) ||
       !kzsf_character_bind_worlds(plan,source_world,world))return 0;
    return write(opaque,a,world,sizeof(world)) && write(opaque,b,world,sizeof(world));
}
#endif
