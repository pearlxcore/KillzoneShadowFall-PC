#define main query_fixture_checks
#include "test_kzsf_character_native_query.c"
#undef main
#include "../src/kzsf_character_retarget.h"
static unsigned char map_code[20];static int builds,map_releases;static unsigned freed;
static int map_read(void *opaque,uintptr_t p,void *out,size_t n) {
    if(p==origin+0x5d8dd0 && n==20){memcpy(out,map_code,n);return 1;}
    if(unreadable && p==origin+0x6000)return 0;
    return read_memory(opaque,p,out,n);
}
static void KZSF_CHARACTER_ABI build_map(uintptr_t source,uintptr_t destination,KzsfCharacterNativeMap *map) {
    assert(source==origin+0x1000 && destination==origin+0x2000);
    assert(!map->source && !map->destination && !map->joints.data && !map->channels.data);builds++;
    map->source=source;map->destination=destination;
    map->joints=(KzsfCharacterNativeArray){mode==2 ? 3 : 4,0,origin+0x6000,4,0};
    map->channels=(KzsfCharacterNativeArray){2,0,mode==3 ? origin+0x6000 : origin+0x7000,2,0};
    if(mode==5)map->channels=(KzsfCharacterNativeArray){0};
    KzsfCharacterJointMap joints[4]={{0,0,{0}},{2,1,{0}},{-1,255,{255,255,255}},{1,0,{0}}};
    if(mode==1)joints[0].source=3;
    if(mode==4)joints[0].parent_differs=2;
    memcpy(memory+0x6000,joints,sizeof(joints));
}
static void KZSF_CHARACTER_ABI map_release(uintptr_t p) {
    unsigned bit=p==origin+0x6000 ? 1 : p==origin+0x7000 ? 2 : 0;
    assert(bit && !(freed & bit));freed|=bit;map_releases++;
}
static void map_setup(void) {
    setup();builds=map_releases=0;freed=0;
    int32_t source=3,destination=4;
    memcpy(memory+0x1030,&source,4);memcpy(memory+0x2030,&destination,4);
}
int main(void) {
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&size,8,1,f)==1);
    assert(!fseek(f,0,SEEK_END));long start=ftell(f)-(long)size;
    assert(!fseek(f,start+0x39b350,SEEK_SET));assert(fread(query_code,1,20,f)==20);
    assert(!fseek(f,start+0x3295d0,SEEK_SET));assert(fread(free_code,1,17,f)==17);
    assert(!fseek(f,start+0x5d8dd0,SEEK_SET));assert(fread(map_code,1,20,f)==20);fclose(f);
    KzsfCharacterMapApi api={build_map,map_release};KzsfCharacterRetargetAudit out,before;const char *why=NULL;
    map_setup();assert(kzsf_character_retarget_audit(origin+0x1000,origin+0x2000,origin,map_read,NULL,api,&out,&why));
    assert(builds==1 && map_releases==2 && freed==3 && out.source_count==3 && out.count==4);
    assert(out.matched==3 && out.missing==1 && out.parent_changes==1 && out.channels==2);
    assert(out.joints[2].source==-1 && !out.joints[2].parent_differs && !out.joints[2].padding[0]);
    memset(&out,0xa5,sizeof(out));before=out;
    for(int i=1;i<=4;i++) {
        map_setup();mode=i;
        assert(!kzsf_character_retarget_audit(origin+0x1000,origin+0x2000,origin,map_read,NULL,api,&out,&why));
        assert(builds==1 && map_releases==(i==3 ? 1 : 2) && !memcmp(&out,&before,sizeof(out)));
    }
    map_setup();unreadable=1;
    assert(!kzsf_character_retarget_audit(origin+0x1000,origin+0x2000,origin,map_read,NULL,api,&out,&why));
    assert(builds==1 && map_releases==2 && !strcmp(why,"retarget-map-entry"));
    map_setup();bad_guard=1;
    assert(!kzsf_character_retarget_audit(origin+0x1000,origin+0x2000,origin,map_read,NULL,api,&out,&why));
    assert(!builds && !map_releases);
    map_setup();mode=5;
    assert(kzsf_character_retarget_audit(origin+0x1000,origin+0x2000,origin,map_read,NULL,api,&out,&why));
    assert(map_releases==1 && !out.channels);
    puts("native retarget audit: direction, changed-parent/missing entries, guards, invalid output and exact cleanup pass");
    return 0;
}
