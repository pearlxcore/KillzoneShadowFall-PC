/* Exact selector layout/retarget gates on live resources; PROCESS_VM_READ only.
 * No guest calls, writes, injected hooks, thread suspension or full-memory scan. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_character_generic_pose.h"
#include "../src/kzsf_character_face.h"
static HANDLE process;
static unsigned reads;
static int memory(void *opaque,uintptr_t address,void *out,size_t size) {
    (void)opaque;SIZE_T received=0;
    return ++reads<=2000000 && size<=65536 && ReadProcessMemory(process,(void*)address,out,size,&received) && received==size;
}
static uintptr_t type(uintptr_t object,uintptr_t base,KzsfZoomRead read,void *opaque) {
    uintptr_t table=0,getter=0;unsigned char code[8];int32_t rel=0;
    if(!kzsf_zoom_read(read,opaque,object,0,&table,8) || !kzsf_zoom_read(read,opaque,table,0,&getter,8) || getter<base ||
       getter-base>0x1190000-8 || !read(opaque,getter,code,8) || code[0]!=0x48 || code[1]!=0x8d || code[2]!=5 || code[7]!=0xc3)return 0;
    memcpy(&rel,code+3,4);return getter+7+rel;
}
int main(int argc,char **argv) {
    if(argc<4)return 2;
    process=OpenProcess(PROCESS_VM_READ,FALSE,(DWORD)strtoul(argv[1],0,0));if(!process)return 3;
    uintptr_t base=UINT64_C(0x800000000),actor=strtoull(argv[2],0,0);
    KzsfCharacterInventory player;KzsfCharacterPart original;KzsfCharacterMeshTree source;
    if(!kzsf_character_inventory_read(actor,memory,0,&player) ||
       !kzsf_character_part_read(player.variants[player.current].part,base,memory,0,type,&original) ||
       !kzsf_character_mesh_read(original.mesh,base,memory,0,type,&source) || !source.count)return 4;
    for(int a=3;a<argc;a++) {
        uintptr_t resource=strtoull(argv[a],0,0);KzsfCharacterInventory variants;char name[128];
        if(!kzsf_character_resource_name(resource,base,memory,0,name) ||
           !kzsf_character_resource_variants_read(resource,base,memory,0,type,&variants))continue;
        for(int v=0;v<variants.count;v++) {
            KzsfCharacterPart part={0};KzsfCharacterMeshTree tree={0};KzsfCharacterGenericPlan forward,back;
            uintptr_t body=variants.variants[v].part,face=0;int uniform=1,fa=0,ba=0,fc=0;int32_t bones=0;
            int layout=kzsf_character_part_read(body,base,memory,0,type,&part) && part.is_skinned && !part.physics &&
                kzsf_character_mesh_read(part.mesh,base,memory,0,type,&tree) && tree.count && !tree.unknown_count;
            if(!layout)fprintf(stderr,"layout %s variant=%d mesh=%llx physics=%llx skinned=%d leaves=%d unknown=%d\n",
                name,v,(unsigned long long)part.mesh,(unsigned long long)part.physics,part.is_skinned,tree.count,tree.unknown_count);
            KzsfCharacterHead head={0};const char *head_reason=0;
            int head_ok=kzsf_character_head_read(resource,player.variants[player.current].part,body,base,memory,0,type,&head,&head_reason);
            if(!head_ok)head_ok=kzsf_character_head_from_model(resource,player.variants[player.current].part,body,base,memory,0,type,&head,&head_reason);
            fprintf(stderr,"head %s variant=%d supported=%d part=%llx reason=%s\n",name,v,head_ok,
                (unsigned long long)head.part,head_reason ? head_reason : "unknown");
            if(layout) {
                kzsf_zoom_read(memory,0,tree.leaves[0].skeleton,0x30,&bones,4);
                for(int i=0;i<tree.count;i++)if(tree.leaves[i].skeleton!=tree.leaves[0].skeleton || tree.leaves[i].binding_count!=bones)uniform=0;
                fa=kzsf_character_generic_plan(&source.leaves[0],&tree.leaves[0],memory,0,&forward);
                ba=kzsf_character_generic_plan(&tree.leaves[0],&source.leaves[0],memory,0,&back);
                fc=kzsf_character_face_resource_variant(resource,body,v,base,memory,0,type,&face);
            }
            printf("{\"name\":\"%s\",\"variant\":%d,\"body\":\"%llx\",\"resource\":\"%llx\",\"layout\":%d,\"leaves\":%d,\"bones\":%d,\"uniform\":%d,\"forward\":%d,\"reverse\":%d,\"matched\":%d,\"face_supported\":%d,\"face\":\"%llx\",\"read_only\":true,\"reads\":%u}\n",
                name,v,(unsigned long long)body,(unsigned long long)resource,layout,tree.count,bones,uniform,fa,ba,fa ? forward.matched : 0,fc,(unsigned long long)face,reads);
        }
    }
    CloseHandle(process);return 0;
}
