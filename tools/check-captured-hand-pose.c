#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_character_generic_pose.h"
typedef struct {uintptr_t address;uint32_t size;unsigned char *data;} Block;
static Block blocks[4096];static uint32_t count;
static int read_capture(void *unused,uintptr_t address,void *out,size_t n) {
    (void)unused;
    for(uint32_t i=0;i<count;i++)if(address>=blocks[i].address && address-blocks[i].address<=blocks[i].size && n<=blocks[i].size-(address-blocks[i].address)) {
        memcpy(out,blocks[i].data+address-blocks[i].address,n);return 1;
    }
    return 0;
}
static int finger(const char *name) {
    const char *prefix[]={"thumb","index","middle","ring","pinky"};
    for(unsigned i=0;i<5;i++)if(!strncmp(name,prefix[i],strlen(prefix[i])))return 1;
    return 0;
}
static float length(const float *p){return sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);}
static float inspect(const char *label,const KzsfCharacterMeshLeaf *leaf,const KzsfCharacterGenericPlan *plan,float worlds[][16]) {
    float maximum=0;
    for(int i=0;i<plan->to_count;i++) {
        char name[129];assert(kzsf_character_joint_name(leaf->skeleton,i,read_capture,NULL,name));
        int parent=plan->parent[i];if(!finger(name) || parent<0)continue;
        float inverse[16],local[16];assert(kzsf_character_matrix_inverse(worlds[parent],inverse));
        assert(kzsf_character_matrix_multiply(inverse,worlds[i],local));
        float expected=length(plan->local[i]+12),actual=length(local+12);
        float ratio=expected>1e-5f ? actual/expected : 1;
        if(ratio>maximum)maximum=ratio;
        if(ratio>1.5f)printf("%s %s length%.5f bind%.5f stretch%.2fx\n",label,name,actual,expected,ratio);
    }
    printf("%s max finger stretch %.4fx\n",label,maximum);return maximum;
}
int main(int argc,char **argv) {
    assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);uint32_t models;KzsfCharacterMeshLeaf leaves[2]={0};
    assert(fread(&models,4,1,f)==1 && models==2);
    for(unsigned i=0;i<models;i++)assert(fread(&leaves[i].skeleton,8,1,f)==1 && fread(&leaves[i].bindings,8,1,f)==1 && fread(&leaves[i].binding_count,4,1,f)==1);
    assert(fread(&count,4,1,f)==1 && count<4096);
    for(uint32_t i=0;i<count;i++) {
        assert(fread(&blocks[i].address,8,1,f)==1 && fread(&blocks[i].size,4,1,f)==1);
        blocks[i].data=malloc(blocks[i].size);assert(blocks[i].data && fread(blocks[i].data,blocks[i].size,1,f)==1);
    }
    fclose(f);float source[KZSF_CHARACTER_GENERIC_MAX][16],target[KZSF_CHARACTER_GENERIC_MAX][16];
    f=fopen(argv[2],"rb");assert(f);assert(fread(source,64,98,f)==98);fclose(f);
    KzsfCharacterGenericPlan base,plan;
    assert(kzsf_character_generic_plan(&leaves[0],&leaves[0],read_capture,NULL,&base));
    assert(kzsf_character_generic_plan(&leaves[0],&leaves[1],read_capture,NULL,&plan));
    assert(kzsf_character_generic_worlds(&plan,source,target));
    inspect("native input",&leaves[0],&base,source);inspect("converted Massar",&leaves[1],&plan,target);
    float fixed[KZSF_CHARACTER_GENERIC_MAX][16];
    assert(kzsf_character_generic_draw_worlds(&plan,source,fixed));
    assert(inspect("anchored Massar",&leaves[1],&plan,fixed)<1.001f);
    for(int i=0;i<plan.to_count;i++) {
        for(int k=0;k<12;k++)assert(fixed[i][k]==target[i][k]);
        if(!plan.finger[i])assert(!memcmp(fixed[i],target[i],64));
    }
    for(uint32_t i=0;i<count;i++)free(blocks[i].data);
}
