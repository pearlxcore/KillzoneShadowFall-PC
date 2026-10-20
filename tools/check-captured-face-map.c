#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_character_face.h"
typedef struct {uintptr_t address;uint32_t size;unsigned char *data;} Block;
static Block blocks[4096];static uint32_t count;
static int read_capture(void *unused,uintptr_t address,void *out,size_t n) {
    (void)unused;
    for(uint32_t i=0;i<count;i++)if(address>=blocks[i].address && address-blocks[i].address<=blocks[i].size &&
        n<=blocks[i].size-(address-blocks[i].address)) {
        memcpy(out,blocks[i].data+address-blocks[i].address,n);return 1;
    }
    return 0;
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uintptr_t variant,actual;
    assert(fread(&variant,8,1,f)==1 && fread(&actual,8,1,f)==1 && fread(&count,4,1,f)==1 && count<4096);
    for(uint32_t i=0;i<count;i++) {
        assert(fread(&blocks[i].address,8,1,f)==1 && fread(&blocks[i].size,4,1,f)==1 && blocks[i].size<=65536);
        blocks[i].data=malloc(blocks[i].size);assert(blocks[i].data && fread(blocks[i].data,blocks[i].size,1,f)==1);
    }
    fclose(f);
    unsigned char prepared[0xf0],original[0xf0];int32_t mapped[512],n;uintptr_t expected,indices;
    assert(read_capture(NULL,variant,original,sizeof(original)));
    memcpy(&expected,original+0x60,8);memcpy(&n,original+0xb8,4);memcpy(&indices,original+0xc0,8);
    assert(kzsf_character_face_reindex(variant,actual,read_capture,NULL,prepared,mapped));
    int matches=0;
    for(int i=0;i<n;i++) {
        int32_t source;assert(read_capture(NULL,indices+(size_t)i*4,&source,4));
        if(source<0){assert(mapped[i]==-1);continue;}
        char a[129],b[129];
        assert(kzsf_character_joint_name(expected,source,read_capture,NULL,a));
        assert(kzsf_character_joint_name(actual,mapped[i],read_capture,NULL,b) && !strcmp(a,b));matches++;
    }
    uintptr_t address=(uintptr_t)mapped;
    memcpy(original+0x60,&actual,8);memcpy(original+0xc0,&address,8);
    assert(!memcmp(original,prepared,sizeof(original)));
    printf("Captured native face map: %d named joints remapped; all native offsets preserved\n",matches);
    for(uint32_t i=0;i<count;i++)free(blocks[i].data);
}
