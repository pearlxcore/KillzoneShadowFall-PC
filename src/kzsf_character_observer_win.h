#ifndef KZSF_CHARACTER_OBSERVER_WIN_H
#define KZSF_CHARACTER_OBSERVER_WIN_H
#include "kzsf_character_native_query.h"
#include "kzsf_character_mesh.h"

/* Included after camera observer helpers. Explicit diagnostic only. */
static inline void kzsf_observer_mesh_tree(FILE *out,uintptr_t mesh,uintptr_t base,
        KzsfZoomRead read,void *opaque) {
    KzsfCharacterMeshTree tree;
    if(!kzsf_character_mesh_read(mesh,base,read,opaque,kzsf_observer_baseline_type,&tree)) {
        fprintf(out,"  mesh_tree root=%p status=unavailable\n",(void*)mesh);return;
    }
    fprintf(out,"  mesh_tree root=%p skinned_leaves=%d unknown_nodes=%d rig_verified=0\n",
        (void*)mesh,tree.count,tree.unknown_count);
    for(int32_t i=0;i<tree.count;i++) {
        KzsfCharacterMeshLeaf *leaf=&tree.leaves[i];
        fprintf(out,"    mesh_leaf mesh=%p skeleton=%p bindings=%p bones=%d animator_hash=%08x layout_hash=%08x\n",
            (void*)leaf->mesh,(void*)leaf->skeleton,(void*)leaf->bindings,leaf->binding_count,
            leaf->animator_hash,leaf->layout_hash);
    }
}
static inline void kzsf_observer_nested_parts(FILE *out,uintptr_t resource,uintptr_t base,
        KzsfZoomRead read,void *opaque,int from_query) {
    KzsfCharacterInventory variants;char name[128];
    fprintf(out,"  soldier_resource resource=%p source=%s\n",(void*)resource,from_query ? "native-query" : "active-player");
    if(kzsf_character_resource_name(resource,base,read,opaque,name))fprintf(out,"    soldier_resource_name=%s\n",name);
    if(!kzsf_character_resource_variants_read(resource,base,read,opaque,kzsf_observer_baseline_type,&variants)) {
        fputs("    soldier_variants status=unavailable\n",out);return;
    }
    fprintf(out,"    soldier_variants count=%d detail_limit=8\n",variants.count);
    for(int32_t i=0;i<variants.count && i<8;i++) {
        uintptr_t p=variants.variants[i].part;KzsfCharacterPart part;
        fprintf(out,"  nested_part resource=%p soldier_resource=%p variant=%d source=%s\n",
            (void*)p,(void*)resource,i,from_query ? "native-query" : "active-player");
        if(!p)continue;
        if(kzsf_character_resource_name(p,base,read,opaque,name))fprintf(out,"    resource_name=%s\n",name);
        if(!kzsf_character_part_read(p,base,read,opaque,kzsf_observer_baseline_type,&part)) {
            fputs("    part_status=unavailable\n",out);continue;
        }
        fprintf(out,"    part_mesh=%p physics=%p is_skinned=%u\n",(void*)part.mesh,(void*)part.physics,part.is_skinned);
        if(part.mesh && part.is_skinned)kzsf_observer_mesh_tree(out,part.mesh,base,read,opaque);
    }
}
static inline int kzsf_observer_character_resources(FILE *out,uintptr_t owner,uintptr_t base,
        KzsfZoomRead read,void *opaque,uintptr_t tcb) {
    KzsfCharacterNativeResources resources;
    fprintf(out,"KILLZONE_CHARACTER_RESOURCES begin owner=%p read_only=1 enumeration=engine-query\n",(void*)owner);
    KzsfCharacterInventory inventory;uintptr_t player_resource=0;
    if(kzsf_character_inventory_read(owner,read,opaque,&inventory)) {
        player_resource=inventory.resource;
        uintptr_t part=inventory.variants[inventory.current].part;
        fprintf(out,"  player_current_part=%p variant_count=%d\n",(void*)part,inventory.count);
        KzsfCharacterPart body;
        if(kzsf_character_part_read(part,base,read,opaque,kzsf_observer_baseline_type,&body)) {
            fprintf(out,"  player_mesh=%p physics=%p is_skinned=%u\n",(void*)body.mesh,(void*)body.physics,body.is_skinned);
            kzsf_observer_object(out,"player-part-mesh",body.mesh,base,read,opaque);
            kzsf_observer_mesh_tree(out,body.mesh,base,read,opaque);
        } else fputs("  player_part_status=unavailable\n",out);
    }
    KzsfCameraEngineContext context;
    if(!kzsf_camera_engine_context(tcb,base,read,opaque,&context) || context.allocate_method!=base+0x4f67a0) {
        fputs("KILLZONE_CHARACTER_RESOURCES end status=unavailable reason=engine-context\n",out);return 0;
    }
    KzsfCharacterQueryApi api={(KzsfCharacterQuery)(base+0x39b350),(KzsfCharacterQueryFree)(base+0x3295d0)};
    const char *why="unknown";
    fputs("  native_resource_query phase=begin\n",out);fflush(out);
    if(!kzsf_character_native_query(base,read,opaque,kzsf_observer_baseline_type,api,&resources,&why)) {
        fprintf(out,"KILLZONE_CHARACTER_RESOURCES end status=unavailable reason=%s\n",why);return 0;
    }
    fprintf(out,"  loaded_parts count=%d manager=%p source=native-query native_total=%d unknown_types=%d duplicates=%d truncated=%d\n",
        resources.count,(void*)resources.manager,resources.total,resources.unknown_types,resources.duplicates,
        resources.total>KZSF_CHARACTER_NATIVE_LIMIT);
    uintptr_t roots[128]={0};unsigned root_count=0;
    for(int32_t i=0;i<resources.count;i++) {
        uintptr_t resource=resources.resources[i];KzsfCharacterPart part;
        fprintf(out,"  loaded_part resource=%p source=native-query\n",(void*)resource);
        char name[128];
        if(kzsf_character_resource_name(resource,base,read,opaque,name))fprintf(out,"    resource_name=%s\n",name);
        if(!kzsf_character_part_read(resource,base,read,opaque,kzsf_observer_baseline_type,&part)) {
            fputs("    part_status=unavailable\n",out);continue;
        }
        fprintf(out,"    part_mesh=%p physics=%p is_skinned=%u\n",(void*)part.mesh,(void*)part.physics,part.is_skinned);
        /* Scenery and physics-only parts must not consume the character mesh
         * detail budget before the player/NPC parts later in the native list. */
        if(!part.is_skinned)continue;
        kzsf_observer_object(out,"loaded-part-mesh",part.mesh,base,read,opaque);
        int duplicate=0;
        for(unsigned j=0;j<root_count;j++)if(roots[j]==part.mesh){duplicate=1;break;}
        if(duplicate)continue;
        if(root_count==128){fputs("    mesh_detail_limit_reached=1\n",out);continue;}
        roots[root_count++]=part.mesh;
        kzsf_observer_mesh_tree(out,part.mesh,base,read,opaque);
    }
    /* Separate named character resources from standalone world parts. The
     * player resource is a sentinel: injection is logged distinctly and can
     * never be counted as query coverage. */
    if(player_resource)kzsf_observer_nested_parts(out,player_resource,base,read,opaque,0);
    fputs("  native_soldier_query phase=begin\n",out);fflush(out);
    if(kzsf_character_native_query_type(base,0x3caac10,read,opaque,kzsf_observer_baseline_type,api,&resources,&why)) {
        int found=0;
        for(int32_t i=0;i<resources.count;i++)if(resources.resources[i]==player_resource)found=1;
        fprintf(out,"  loaded_soldiers count=%d native_total=%d unknown_types=%d truncated=%d player_resource_found=%d detail_limit=32\n",
            resources.count,resources.total,resources.unknown_types,resources.total>KZSF_CHARACTER_NATIVE_LIMIT,found);
        for(int32_t i=0;i<resources.count && i<32;i++)
            kzsf_observer_nested_parts(out,resources.resources[i],base,read,opaque,1);
    } else fprintf(out,"  loaded_soldiers status=unavailable reason=%s\n",why);
    fputs("KILLZONE_CHARACTER_RESOURCES end status=sampled rig_verified=0\n",out);return 1;
}
#endif
