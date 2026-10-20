#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_character_bundle_loader.h"
#include "../src/kzsf_character_render_bridge.h"
#include "../src/kzsf_checkpoint_handle_bridge.h"

static unsigned char *image;
static uintptr_t base,manager,worker,entry,string;
static int registrations,queues,unloads,resident_releases;
static uintptr_t dependency;
static uintptr_t soldier;
static uintptr_t resource_type(uintptr_t address,uintptr_t origin,KzsfZoomRead read,void *opaque){
    (void)read;(void)opaque;return address==soldier || address==soldier+0x100 ? origin+0x3caac10 : 0;
}
static void put64(uintptr_t address,uintptr_t value){memcpy((void*)address,&value,8);}
static void put32(uintptr_t address,int32_t value){memcpy((void*)address,&value,4);}
static int memory(void *opaque,uintptr_t address,void *out,size_t size) {
    (void)opaque;
    if(address<base || address-base>0x4000000 || size>0x4000000-(address-base))return 0;
    memcpy(out,(void*)address,size);return 1;
}
static void KZSF_CHARACTER_ABI init_string(uintptr_t *out){*out=string;}
static void KZSF_CHARACTER_ABI set_string(uintptr_t *out,const char *name){
    assert(*out==string);put32(string+8,(int32_t)strlen(name));strcpy((char*)string+16,name);
}
static void KZSF_CHARACTER_ABI free_string(uintptr_t *out){assert(*out==string);}
static void KZSF_CHARACTER_ABI register_bundle(uintptr_t target,const KzsfCharacterBundleDescriptor *d,int flags){
    assert(target==manager && d->kind==1 && d->resident==1 && !d->mode && !d->stream &&
        !d->dependencies.count && d->priority==-1 && !flags);
    registrations++;put32(manager+8,1);put64(manager+0x10,base+0x1e02000);
    put64(base+0x1e02000,entry);put64(entry+0x10,string);
    put32(entry+4,1);
}
static void KZSF_CHARACTER_ABI queue_bundle(uintptr_t target,const KzsfCharacterNativeArray *array,uintptr_t callback,uintptr_t data,int mode){
    assert(target==worker && array->count==1 && array->capacity==1 && !callback && !data && !mode);
    assert(*(uintptr_t*)array->data==entry);queues++;
}
static void KZSF_CHARACTER_ABI release_resident(uintptr_t target,uintptr_t lump,uintptr_t output){
    assert(target==manager && lump==entry && !output);
    int secondary=*(int32_t*)(lump+4);assert(secondary>0);
    put32(lump+4,secondary-1);resident_releases++;
}
static void KZSF_CHARACTER_ABI release_primary(uintptr_t target,uintptr_t lump){
    assert(target==manager && lump==entry && resident_releases==unloads+1);
    int primary=*(int32_t*)lump;assert(primary>0);
    put32(lump,primary-1);if(primary==1)put32(manager+8,0);unloads++;
}
static void KZSF_CHARACTER_ABI wait_bundle(uintptr_t target){assert(target==worker);}
static uint64_t u64(const unsigned char *p){uint64_t v;memcpy(&v,p,8);return v;}
static uint16_t u16(const unsigned char *p){uint16_t v;memcpy(&v,p,2);return v;}
int main(int argc,char **argv){
    assert(argc==2);FILE *elf=fopen(argv[1],"rb");assert(elf);
    unsigned char header[64];assert(fread(header,1,64,elf)==64 && !memcmp(header,"\177ELF",4));
    image=calloc(1,0x4000000);assert(image);base=(uintptr_t)image;
    uint64_t phoff=u64(header+32);unsigned ents=u16(header+54),count=u16(header+56);
    /* Verify every native signature against the shipped ELF, rather than
     * reproducing those bytes in the fixture. No guest code is executed. */
    for(unsigned i=0;i<count;i++){
        unsigned char ph[56];assert(!fseek(elf,(long)(phoff+i*ents),SEEK_SET));assert(fread(ph,1,56,elf)==56);
        uint32_t type;memcpy(&type,ph,4);if(type!=1)continue;
        uint64_t offset=u64(ph+8),address=u64(ph+16),size=u64(ph+32);
        assert(address<=0x4000000 && size<=0x4000000-address);
        assert(!fseek(elf,(long)offset,SEEK_SET));assert(fread(image+address,1,(size_t)size,elf)==size);
    }
    fclose(elf);assert(kzsf_character_bundle_guards(base,memory,NULL));
    assert(kzsf_checkpoint_handle_guards(image,0x4000000));
    image[0x62070f]^=1;assert(!kzsf_checkpoint_handle_guards(image,0x4000000));image[0x62070f]^=1;
    manager=base+0x1e00000;worker=base+0x1e01000;entry=base+0x1e03000;string=base+0x1e04000;
    put64(base+0x3c67760,manager);put64(manager,base+0x1637b80);put64(base+0x1637b90,base+0x61c3a0);
    put64(manager+0xd8,worker);put64(worker+8,manager);*(unsigned char*)worker=1;
    KzsfCharacterBundleApi native=kzsf_character_bundle_api(base);
    assert((uintptr_t)native.release_resident==base+0x61d210 && (uintptr_t)native.release_primary==base+0x61d350);
    KzsfCharacterBundleApi api={init_string,set_string,free_string,register_bundle,queue_bundle,release_resident,release_primary,wait_bundle};
    KzsfCharacterBundleLease lease={0};const char *why=NULL;
    *(unsigned char*)(worker+0x88)=1;
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==0 && !queues && !registrations);
    *(unsigned char*)(worker+0x88)=0;
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==2 && queues==1 && registrations==1);
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==0 && queues==1);
    put32(entry,1);*(unsigned char*)(entry+0x30)=1;
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==1 && queues==1);
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==1 && queues==1);
    assert(kzsf_character_bundle_request(0,base,memory,NULL,api,&lease,&why)==-1 && queues==1);
    soldier=base+0x1e05000;uintptr_t names=base+0x1e07000,objects=base+0x1e08000,found=0;
    put64(soldier,base+0x1e06000);put64(base+0x1e06018,base+0x33b790);
    put64(soldier+0x10,names);put32(names+8,12);strcpy((char*)names+16,"VSA_Engineer");
    put64(soldier+0x100,base+0x1e06000);put64(soldier+0x110,names);
    put32(entry+0x18,3);put64(entry+0x20,objects);
    put64(objects,base+0x1e09000);put64(objects+8,soldier);put64(objects+16,soldier);
    assert(kzsf_character_bundle_soldier(1,base,memory,NULL,resource_type,&lease,&found)==1 && found==soldier);
    assert(lease.soldier==soldier); // discovery works without a global-query entry
    *(unsigned char*)(worker+0x88)=1;
    assert(kzsf_character_bundle_soldier(1,base,memory,NULL,resource_type,&lease,&found)==0);
    *(unsigned char*)(worker+0x88)=0;
    put32(entry+0x18,20001);
    assert(kzsf_character_bundle_soldier(1,base,memory,NULL,resource_type,&lease,&found)==-1);
    put32(entry+0x18,3);put32(names+8,5);strcpy((char*)names+16,"wrong");
    assert(kzsf_character_bundle_soldier(1,base,memory,NULL,resource_type,&lease,&found)==-1);
    put32(names+8,12);strcpy((char*)names+16,"VSA_Engineer");
    lease.soldier=0;put64(objects+16,soldier+0x100);
    assert(kzsf_character_bundle_soldier(1,base,memory,NULL,resource_type,&lease,&found)==-1); // ambiguous identity
    put64(objects+16,soldier);
    assert(kzsf_character_bundle_soldier(1,base,memory,NULL,resource_type,&lease,&found)==1);
    strcpy((char*)string+16,"wrong bundle");
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==-1 && queues==1);
    image[0x61cd70]^=1;
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==-1 && queues==1);
    image[0x61cd70]^=1;set_string(&string,kzsf_character_model_bundle(1));
    *(unsigned char*)(worker+0x88)=1;
    assert(kzsf_character_bundle_release(1,base,memory,NULL,api,&lease)==0 && !unloads);
    *(unsigned char*)(worker+0x88)=0;
    /* Native registration may record dependencies already owned by the level
     * without taking a primary reference on them. Descriptor61d090 recursively
     * releases these on the final primary release; that would consume the
     * level's only reference. Our paired primitive release must not do so. */
    dependency=base+0x1e0b000;
    put32(dependency,1);put32(dependency+4,1);
    put32(entry+0x40,1);put64(entry+0x48,base+0x1e0c000);put64(base+0x1e0c000,dependency);
    assert(kzsf_character_bundle_release(1,base,memory,NULL,api,&lease)==1 && unloads==1 && !lease.queued && !lease.soldier);
    assert(*(int32_t*)dependency==1 && *(int32_t*)(dependency+4)==1 && resident_releases==1);
    assert(kzsf_character_bundle_release(1,base,memory,NULL,api,&lease)==1 && unloads==1);
    assert(kzsf_character_bundle_request(1,base,memory,NULL,api,&lease,&why)==2 && queues==2 && registrations==2);
    /* Native61c8f0 inserts again only when both counts are zero. Registration
     * must hold the secondary count before the worker takes its primary one. */
    assert(*(int32_t*)entry==0 && *(int32_t*)(entry+4)==1 && *(int32_t*)(manager+8)==1);
    put32(entry,2);put32(entry+4,2); // one game-owned reference, one imported lease
    assert(kzsf_character_bundle_release(1,base,memory,NULL,api,&lease)==1 && unloads==2);
    assert(*(int32_t*)entry==1 && *(int32_t*)(entry+4)==1); // preserve the game's residency
    assert(*(int32_t*)dependency==1 && *(int32_t*)(dependency+4)==1 && resident_releases==2);
    unsigned char exit_patch[20],*stub=image+0x1e0a000;
    assert(kzsf_character_level_exit_bridge(stub,25,(uintptr_t)stub,exit_patch,base+KZSF_CHARACTER_LEVEL_EXIT,
        image+KZSF_CHARACTER_LEVEL_EXIT,UINT64_C(0x123456789)));
    assert(!memcmp(stub,image+KZSF_CHARACTER_LEVEL_EXIT,20) && stub[20]==0xe9);
    int32_t back=0;memcpy(&back,stub+21,4);
    assert((uintptr_t)stub+25+back==base+KZSF_CHARACTER_LEVEL_EXIT+20);
    assert(exit_patch[0]==0x48 && exit_patch[1]==0xb8 && u64(exit_patch+2)==UINT64_C(0x123456789));
    unsigned char wrong[20];memcpy(wrong,image+KZSF_CHARACTER_LEVEL_EXIT,20);wrong[0]^=1;
    assert(!kzsf_character_level_exit_bridge(stub,25,(uintptr_t)stub,exit_patch,base+KZSF_CHARACTER_LEVEL_EXIT,wrong,1));
    free(image);puts("bundle signatures and asynchronous lease transitions passed");
}
