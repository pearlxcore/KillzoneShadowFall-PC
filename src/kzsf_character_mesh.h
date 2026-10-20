#ifndef KZSF_CHARACTER_MESH_H
#define KZSF_CHARACTER_MESH_H
#include "kzsf_character_inventory.h"

/* Native metadata: LOD/Multi/Switch arrays at +58/+60, child Mesh at00,
 * strides10/50/10. Regular skinned mesh base: Skeleton+58, bindings+70.
 * Registration5d1960 renames NG RTTI3c1e490 to RegularSkinnedMeshResource;
 * base RTTI3c1e570 keeps the same inherited skeleton/bindings layout.
 * This records links and hashes, NOT proof of animation/bone compatibility. */
/* Live VSA Security Trooper has164 distinct skinned leaves (195 visits).
 * Keep the traversal visit/depth limits while accommodating its full tree. */
enum { KZSF_CHARACTER_MAX_MESHES=256 };
typedef struct {
    uintptr_t mesh,skeleton,bindings;
    uint32_t animator_hash,layout_hash;
    int32_t binding_count;
} KzsfCharacterMeshLeaf;
typedef struct {
    uintptr_t root;
    int32_t count,unknown_count;
    KzsfCharacterMeshLeaf leaves[KZSF_CHARACTER_MAX_MESHES];
} KzsfCharacterMeshTree;
typedef struct {
    KzsfCharacterMeshTree result;
    uintptr_t ancestors[9];
    unsigned visits;
} KzsfCharacterMeshWalk;
static inline int kzsf_character_mesh_walk(KzsfCharacterMeshWalk *walk,uintptr_t mesh,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterType type,unsigned depth) {
    if(!mesh || depth>=9 || ++walk->visits>512)return 0;
    for(unsigned i=0;i<depth;i++)if(walk->ancestors[i]==mesh)return 0;
    walk->ancestors[depth]=mesh;
    uintptr_t t=type(mesh,base,read,opaque);
    if(!t)return 0;
    if(t==base+0x3c1e490 || t==base+0x3c1e570) {
        for(int32_t i=0;i<walk->result.count;i++)if(walk->result.leaves[i].mesh==mesh)return 1;
        if(walk->result.count==KZSF_CHARACTER_MAX_MESHES)return 0;
        KzsfCharacterMeshLeaf *leaf=&walk->result.leaves[walk->result.count++];leaf->mesh=mesh;
        int32_t matrices=0;uintptr_t names=0,matrix_data=0;
        if(!kzsf_zoom_read(read,opaque,mesh,0x58,&leaf->skeleton,8) ||
           type(leaf->skeleton,base,read,opaque)!=base+0x3c1ec70 ||
           !kzsf_zoom_read(read,opaque,mesh,0x70,&leaf->bindings,8) ||
           type(leaf->bindings,base,read,opaque)!=base+0x3c1e730 ||
           !kzsf_zoom_read(read,opaque,leaf->skeleton,0xc8,&leaf->animator_hash,4) ||
           !kzsf_zoom_read(read,opaque,leaf->skeleton,0xcc,&leaf->layout_hash,4) ||
           !kzsf_zoom_read(read,opaque,leaf->bindings,0x10,&leaf->binding_count,4) ||
           leaf->binding_count<=0 || leaf->binding_count>4096 ||
           !kzsf_zoom_read(read,opaque,leaf->bindings,0x18,&names,8) || !names ||
           !kzsf_zoom_read(read,opaque,leaf->bindings,0x28,&matrices,4) || matrices!=leaf->binding_count ||
           !kzsf_zoom_read(read,opaque,leaf->bindings,0x30,&matrix_data,8) || !matrix_data)return 0;
        return 1;
    }
    size_t stride=t==base+0x3b4feb8 ? 0x50 :
        (t==base+0x3b50078 || t==base+0x3b50778) ? 0x10 : 0;
    if(!stride){walk->result.unknown_count++;return 1;}
    int32_t count=0;uintptr_t array=0;
    if(!kzsf_zoom_read(read,opaque,mesh,0x58,&count,4) || count<0 || count>128 ||
       !kzsf_zoom_read(read,opaque,mesh,0x60,&array,8) || (count && !array))return 0;
    for(int32_t i=0;i<count;i++) {
        uintptr_t child=0;
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*stride,&child,8) ||
           !kzsf_character_mesh_walk(walk,child,base,read,opaque,type,depth+1))return 0;
    }
    return 1;
}
static inline int kzsf_character_mesh_collect(uintptr_t mesh,uintptr_t base,KzsfZoomRead read,
        void *opaque,KzsfCharacterType type,KzsfCharacterMeshTree *out) {
    KzsfCharacterMeshWalk walk;memset(&walk,0,sizeof(walk));walk.result.root=mesh;
    if(!read || !type || !out || base>UINTPTR_MAX-0x3c1ec70 ||
       !kzsf_character_mesh_walk(&walk,mesh,base,read,opaque,type,0))return 0;
    *out=walk.result;return 1;
}
static inline int kzsf_character_mesh_read(uintptr_t mesh,uintptr_t base,KzsfZoomRead read,
        void *opaque,KzsfCharacterType type,KzsfCharacterMeshTree *out) {
    KzsfCharacterMeshTree first,second;
    if(!out || !kzsf_character_mesh_collect(mesh,base,read,opaque,type,&first) ||
       !kzsf_character_mesh_collect(mesh,base,read,opaque,type,&second) ||
       memcmp(&first,&second,sizeof(first)))return 0;
    *out=first;return 1;
}
#endif
