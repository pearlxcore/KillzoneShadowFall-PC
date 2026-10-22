#ifndef KZSF_CUSTOM_MODEL_PROBE_H
#define KZSF_CUSTOM_MODEL_PROBE_H
#include "kzsf_custom_model_manifest.h"
#include "kzsf_custom_model_geometry.h"
#include "kzsf_custom_bounds.h"
#include "kzsf_custom_material_maps.h"

typedef struct {
    unsigned char *vertices[KZSF_CUSTOM_MODEL_LIMIT],*indices[KZSF_CUSTOM_MODEL_LIMIT],*textures[KZSF_CUSTOM_MODEL_LIMIT];
} KzsfCustomModelInput;
static int kzsf_custom_model_upload_guards(KzsfCustomProbeContext *context) {
    const struct {uint32_t rva;unsigned char code[16];unsigned bytes;} guards[]={
        {0x52f140,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x34,0x48,0x89},16},
        {0x500e90,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x20,0x49},16},
        {0x52ef10,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x66,0x48,0x89},16},
        {0x52efa0,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8d,0x05,0x10,0x85,0x0e,0x01},16},
        {0x2cea70,{0x48,0x8d,0x05,0x69,0x24,0x21,0x01,0x48,0x83,0xc0,0x10,0x48,0x89,0x07},14},
        {0x2d0db0,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53},13},
        {0x2ceb70,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0xe8,0x12,0,0,0},14},
        {0x598b10,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x3c,0x48,0x89},16}};
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
        unsigned char code[16];
        if(!kzsf_zoom_read(context->read,context->opaque,context->base,guards[i].rva,code,guards[i].bytes) ||
           memcmp(code,guards[i].code,guards[i].bytes))return 0;
    }
    return 1;
}
static unsigned char *kzsf_custom_model_file(const char *folder,const char *name,size_t expected,uint32_t crc,int checksum) {
    char path[4096];int n=snprintf(path,sizeof(path),"%s/%s",folder,name);
    if(n<0 || (size_t)n>=sizeof(path) || expected>64u*1024u*1024u+16)return NULL;
    FILE *file=fopen(path,"rb");if(!file)return NULL;
    long size=-1;if(!fseek(file,0,SEEK_END))size=ftell(file);
    if(size<0 || (size_t)size!=expected || fseek(file,0,SEEK_SET)){fclose(file);return NULL;}
    unsigned char *data=malloc(expected);int okay=data && fread(data,1,expected,file)==expected;fclose(file);
    if(!okay || (checksum && kzsf_custom_crc32(0,data,expected)!=crc)){free(data);return NULL;}return data;
}
static int kzsf_custom_model_rig(uintptr_t bindings,uint32_t expected,KzsfCustomProbeContext *context) {
    int32_t count=0,matrices=0;uintptr_t names=0,inverse=0;uint32_t crc=0;
    KzsfZoomRead read=context->read;void *opaque=context->opaque;
    if(!kzsf_zoom_read(read,opaque,bindings,0x10,&count,4) || count!=98 ||
       !kzsf_zoom_read(read,opaque,bindings,0x28,&matrices,4) || matrices!=count ||
       !kzsf_zoom_read(read,opaque,bindings,0x18,&names,8) ||
       !kzsf_zoom_read(read,opaque,bindings,0x30,&inverse,8))return 0;
    for(int32_t i=0;i<count;i++) {
        uintptr_t name=0;int32_t length=0;unsigned char text[128],matrix[64];
        if(!kzsf_zoom_read(read,opaque,names,(size_t)i*8,&name,8) ||
           !kzsf_zoom_read(read,opaque,name,8,&length,4) || length<1 || length>128 ||
           !kzsf_zoom_read(read,opaque,name,16,text,(size_t)length) ||
           !kzsf_zoom_read(read,opaque,inverse,(size_t)i*64,matrix,64))return 0;
        crc=kzsf_custom_crc32(crc,&length,4);crc=kzsf_custom_crc32(crc,text,(size_t)length);crc=kzsf_custom_crc32(crc,matrix,64);
    }
    if(crc!=expected){fprintf(stderr,"KILLZONE_CUSTOM_MODEL_PROBE rig_crc=%08x expected=%08x\n",crc,expected);fflush(stderr);}
    return crc==expected;
}
static int kzsf_custom_model_binding_name(uintptr_t string,const char *expected,KzsfCustomProbeContext *context) {
    int32_t length=0;char text[129]={0};
    return kzsf_zoom_read(context->read,context->opaque,string,8,&length,4) && length>0 && length<=128 &&
        kzsf_zoom_read(context->read,context->opaque,string,16,text,(size_t)length) && !strcmp(text,expected);
}
static int kzsf_custom_model_rebind(KzsfMaterialTemplate *source,uintptr_t color,const uintptr_t neutral_maps[4],KzsfCustomProbeContext *context,int weighted) {
    int colors=0;
    for(int32_t i=0;i<source->count;i++) {
        KzsfMaterialTexture *bindings=(void *)source->entries[i].textures;
        for(int32_t j=0;j<source->entries[i].texture_count;j++) {
            if(kzsf_custom_model_binding_name(bindings[j].name,"inSampler_texturesetColorAlpha",context) ||
               kzsf_custom_model_binding_name(bindings[j].name,"inSampler_texturesetColor",context)) {bindings[j].texture=color;colors++;}
            else if(kzsf_custom_model_binding_name(bindings[j].name,"inSampler_texturesetNormal",context))bindings[j].texture=neutral_maps[0];
            else if(!weighted)return 0;
            else {
                int32_t length=0;char name[129]={0};
                if(!kzsf_zoom_read(context->read,context->opaque,bindings[j].name,8,&length,4) || length<1 || length>128 ||
                   !kzsf_zoom_read(context->read,context->opaque,bindings[j].name,16,name,(size_t)length))return 0;
                int role=kzsf_custom_material_map_role(name);
                if(role>=KZSF_MAP_NORMAL && role<=KZSF_MAP_WHITE)bindings[j].texture=neutral_maps[role-KZSF_MAP_NORMAL];
                else if(role!=KZSF_MAP_LIGHTING)return 0;
            }
        }
    }
    return colors>0;
}
/* Full asset assembly, still detached. All input files are validated before
 * creating engine resources; every failure unwinds the resources already made. */
static uintptr_t kzsf_custom_model_load(const char *folder,uintptr_t owner,KzsfCustomProbeContext *context,KzsfCharacterType type,uintptr_t *original,int managed) {
    uintptr_t base=context->base;KzsfZoomRead read=context->read;void *opaque=context->opaque;
    KzsfCustomModelManifest manifest;unsigned char header[864];char path[4096];const char *why="manifest";
    int n=snprintf(path,sizeof(path),"%s/model.kzmd",folder);if(n<0 || (size_t)n>=sizeof(path))return 0;
    FILE *file=fopen(path,"rb");if(!file)return 0;
    size_t size=fread(header,1,sizeof(header),file);int end=fgetc(file);fclose(file);
    if(end!=EOF || !kzsf_custom_model_manifest(header,size,&manifest))return 0;
    if(original && !(manifest.flags&1))return 0;
    KzsfCustomModelInput input={0};KzsfCustomMeshSection sections[16]={0};uintptr_t textures[16]={0},neutral_maps[4]={0},part=0,custom_bounds=0;
    KzsfMaterialTemplate *source=NULL;float lo[16][3],hi[16][3];
    const char *weighted_env=getenv("KZSF_CUSTOM_MODEL_WEIGHTED_MATERIAL");
    int weighted=managed || (weighted_env && !strcmp(weighted_env,"1"));
    KzsfCustomRelease release=(KzsfCustomRelease)(base+0x330f20);
    KzsfCharacterInventory inventory;KzsfCharacterPart native;KzsfCharacterMeshTree tree;uintptr_t bounds=0;
    why="native-rig";
    if(!kzsf_custom_model_upload_guards(context) || !kzsf_custom_mesh_guards(context) || !kzsf_character_inventory_read(owner,read,opaque,&inventory) ||
       !kzsf_character_part_read(inventory.variants[inventory.current].part,base,read,opaque,type,&native) ||
       !kzsf_character_mesh_read(native.mesh,base,read,opaque,type,&tree) || !tree.count ||
       !kzsf_custom_model_rig(tree.leaves[0].bindings,manifest.profile,context) ||
       !kzsf_zoom_read(read,opaque,tree.leaves[0].mesh,0x78,&bounds,8) || type(bounds,base,read,opaque)!=base+0x3c1e810)goto cleanup;
    why="source-material";
    source=weighted ? kzsf_custom_material_weighted_snapshot(&tree,base,read,opaque,type) :
        kzsf_custom_material_owner_snapshot(owner,base,read,opaque,type);
    if(!source)goto cleanup;
    why="vertex-index-input";
    for(uint32_t i=0;i<manifest.sections;i++) {
        char name[64];const KzsfCustomModelSectionRecord *s=&manifest.section[i];KzsfCustomIndexLayout il;
        if(weighted && s->vertex_bytes!=68+(uint64_t)s->vertices*44){why="weighted-material-requires-eight-index";goto cleanup;}
        snprintf(name,sizeof(name),"primitive-%u.va",i);input.vertices[i]=kzsf_custom_model_file(folder,name,s->vertex_bytes,s->vertex_crc,1);
        snprintf(name,sizeof(name),"primitive-%u.ia",i);input.indices[i]=kzsf_custom_model_file(folder,name,s->index_bytes,s->index_crc,1);
        if(!input.vertices[i] || !input.indices[i] || !kzsf_custom_model_vertices(input.vertices[i],s->vertex_bytes,s->vertices,lo[i],hi[i]) ||
           !kzsf_custom_index_layout(input.indices[i],s->index_bytes,s->vertices,&il) || il.indices!=s->indices)goto cleanup;
    }
    float minimum[3]={INFINITY,INFINITY,INFINITY},maximum[3]={-INFINITY,-INFINITY,-INFINITY};
    for(uint32_t i=0;i<manifest.sections;i++)for(unsigned axis=0;axis<3;axis++) {
        if(lo[i][axis]<minimum[axis])minimum[axis]=lo[i][axis];if(hi[i][axis]>maximum[axis])maximum[axis]=hi[i][axis];
    }
    why="texture-input";
    for(uint32_t i=0;i<manifest.textures;i++) {
        char name[64];KzsfCustomTextureLayout tl;const KzsfCustomModelTextureRecord *t=&manifest.texture[i];
        snprintf(name,sizeof(name),"texture-%u.rgba",i);input.textures[i]=kzsf_custom_model_file(folder,name,t->bytes,t->crc,1);
        if(!input.textures[i] || !kzsf_custom_texture_layout(input.textures[i],t->bytes,&tl))goto cleanup;
    }
    KzsfCustomOwnedTextureApi texture_api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomTextureConstruct)(base+0x2cea70),
        (KzsfCustomTextureCreate)(base+0x2d0db0),(KzsfCustomRelease)(base+0x2ceb70),
        (KzsfCustomConstruct)(base+0x598b10),release,kzsf_custom_owned_texture_copy,base+0x3b764a0,context};
    why="texture-upload";
    for(uint32_t i=0;i<manifest.textures;i++) {
        textures[i]=kzsf_custom_texture_resource(input.textures[i],manifest.texture[i].bytes,texture_api);if(!textures[i])goto cleanup;
    }
    unsigned char neutral[16+64*64*4];memcpy(neutral,"KZTX",4);uint32_t nh[3]={1,64,64};memcpy(neutral+4,nh,12);
    for(unsigned map=0;map<(weighted?4u:1u);map++) {
        for(unsigned i=16;i<sizeof(neutral);i+=4)memcpy(neutral+i,kzsf_custom_material_neutral_rgba[map],4);
        /* Optional fixed-size local diagnostic maps are read anew at F11.
         * They never overwrite a live resource. Invalid files fail closed. */
        char map_name[64],map_path[4096];
        snprintf(map_name,sizeof(map_name),"neutral-%u.rgba",map);
        int map_length=snprintf(map_path,sizeof(map_path),"%s/%s",folder,map_name);
        if(map_length<0 || (size_t)map_length>=sizeof(map_path)){why="neutral-map-path";goto cleanup;}
        if(GetFileAttributesA(map_path)!=INVALID_FILE_ATTRIBUTES) {
            unsigned char *override=kzsf_custom_model_file(folder,map_name,sizeof(neutral),0,0);
            KzsfCustomTextureLayout neutral_layout;
            if(!override || !kzsf_custom_texture_layout(override,sizeof(neutral),&neutral_layout) ||
               neutral_layout.width!=64 || neutral_layout.height!=64) {
                free(override);why="neutral-map-input";goto cleanup;
            }
            memcpy(neutral,override,sizeof(neutral));free(override);
        }
        fprintf(stderr,"KILLZONE_CUSTOM_MODEL_MATERIAL map=%u rgba=%u,%u,%u,%u crc=%08x\n",map,
            neutral[16],neutral[17],neutral[18],neutral[19],kzsf_custom_crc32(0,neutral,sizeof(neutral)));
        neutral_maps[map]=kzsf_custom_texture_resource(neutral,sizeof(neutral),texture_api);if(!neutral_maps[map])goto cleanup;
    }
    KzsfCustomVertexApi va={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52f1d0),
        (KzsfCustomLoad)(base+0x500d20),release,kzsf_custom_probe_inspect,base+0x3b66818,context};
    KzsfCustomIndexApi ia={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52f140),
        (KzsfCustomLoad)(base+0x500e90),release,kzsf_custom_index_inspect,base+0x3b668f8,context};
    KzsfCustomPrimitiveApi primitive_api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52ef10),
        kzsf_material_native_retain_resource,base+0x3b66c78};
    KzsfMaterialApi material_api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52ede0),release,
        kzsf_material_native_retain_string,kzsf_material_native_retain_resource,base+0x3b64dd8};
    why="section-resources";
    for(uint32_t i=0;i<manifest.sections;i++) {
        const KzsfCustomModelSectionRecord *s=&manifest.section[i];
        if(!kzsf_custom_model_rebind(source,textures[s->texture],neutral_maps,context,weighted))goto cleanup;
        sections[i].effect=kzsf_material_clone(source,material_api);if(!sections[i].effect)goto cleanup;
        uintptr_t v=kzsf_custom_vertex_create(input.vertices[i],s->vertex_bytes,va,NULL);
        uintptr_t ix=v ? kzsf_custom_index_create(input.indices[i],s->index_bytes,s->vertices,ia) : 0;
        if(ix)sections[i].primitive=kzsf_custom_primitive_create(v,ix,s->indices,lo[i],hi[i],primitive_api);
        if(ix)release(ix);if(v)release(v);
        if(!sections[i].primitive)goto cleanup;
        sections[i].vertices=s->vertices;
    }
    KzsfCustomMeshApi mesh_api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x5eb430),
        (KzsfCustomConstruct)(base+0x5eb500),(KzsfCustomConstruct)(base+0x748920),kzsf_material_native_retain_resource,release,
        base+0x3c1e490,base+0x3c1e8f0,base+0x3c3d7b0};
    why="bone-bounds";
    const unsigned char bounds_guard[16]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x48,0x48,0x89};
    unsigned char code[16];uintptr_t inverse=0;float matrices[98*16];
    if(!kzsf_zoom_read(read,opaque,base,0x5e9ae0,code,16) || memcmp(code,bounds_guard,16) ||
       !kzsf_zoom_read(read,opaque,tree.leaves[0].bindings,0x30,&inverse,8) ||
       !kzsf_zoom_read(read,opaque,inverse,0,matrices,sizeof(matrices)))goto cleanup;
    KzsfCustomBoundsApi bounds_api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x5e9ae0),release,base+0x3c1e810};
    custom_bounds=kzsf_custom_bounds_create(minimum,maximum,matrices,98,bounds_api);if(!custom_bounds)goto cleanup;
    why="model-assembly";part=kzsf_custom_model_part_create(tree.leaves[0].skeleton,tree.leaves[0].bindings,custom_bounds,sections,manifest.sections,mesh_api);
    if(part) {
        KzsfCustomSkinnedMesh *mesh=(void *)((KzsfCustomModelPart *)part)->mesh;
        uint32_t native_state[6];
        if(!kzsf_zoom_read(read,opaque,tree.leaves[0].mesh,0x40,native_state,sizeof(native_state)) ||
           !kzsf_custom_mesh_render_config(mesh,native_state,minimum,maximum)) {
            release(part);part=0;why="native-render-header";goto cleanup;
        }
        if(original)*original=inventory.variants[inventory.current].part;
    }
cleanup:
    for(unsigned i=0;i<16;i++) {
        if(sections[i].primitive)release(sections[i].primitive);if(sections[i].effect)release(sections[i].effect);
        if(textures[i])release(textures[i]);free(input.vertices[i]);free(input.indices[i]);free(input.textures[i]);
    }
    for(unsigned i=0;i<4;i++)if(neutral_maps[i])release(neutral_maps[i]);
    if(custom_bounds)release(custom_bounds);kzsf_material_snapshot_free(source);
    fprintf(stderr,"KILLZONE_CUSTOM_MODEL_PROBE status=%s reason=%s sections=%u textures=%u attached=0\n",
        part ? "assembled" : "failed",part ? "complete" : why,manifest.sections,manifest.textures);fflush(stderr);
    return part;
}
static void kzsf_custom_model_probe(uintptr_t owner,KzsfCustomProbeContext *context,KzsfCharacterType type) {
    const char *folder=getenv("KZSF_CUSTOM_MODEL_PROBE_DIR");if(!folder || !*folder)return;
    uintptr_t part=kzsf_custom_model_load(folder,owner,context,type,NULL,0);
    if(part) {
        KzsfCustomModelPart *p=(void *)part;KzsfCustomSkinnedMesh *mesh=(void *)p->mesh;
        fprintf(stderr,"KILLZONE_CUSTOM_MODEL_PROBE graph=complete sections=%d material_slots=%d skin_entries=%d attached=0\n",
            mesh->primitives.count,mesh->effects.count,((KzsfCustomSkinInfo *)mesh->skin)->primitives.count);fflush(stderr);
        ((KzsfCustomRelease)(context->base+0x330f20))(part);
        fputs("KILLZONE_CUSTOM_MODEL_PROBE status=released attached=0 resource_retained=0\n",stderr);fflush(stderr);
    }
}

/* Selector-owned CJ resources, also used by the opt-in F11 diagnostic.
 * Only the diagnostic has a 60-second timeout. Keep caller references until
 * restoration; never invoke a stale actor on unload. */
static uintptr_t kzsf_cj_owner,kzsf_cj_model,kzsf_cj_original,kzsf_cj_part;
static ULONGLONG kzsf_cj_expires;
static const char *kzsf_cj_folder(void) {
    const char *folder=getenv("KZSF_CJ_MODEL_DIR");
    return folder && *folder ? folder : "user/models/cj";
}
static int kzsf_cj_available(void) {
    char path[4096];int n=snprintf(path,sizeof(path),"%s/model.kzmd",kzsf_cj_folder());
    return n>0 && (size_t)n<sizeof(path) && GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES;
}
/* Drop only our owning references, after the native level teardown has removed
 * actors. This never calls a method on an old actor or model. */
static void kzsf_cj_release(uintptr_t base) {
    KzsfCustomRelease release=(KzsfCustomRelease)(base+0x330f20);
    if(kzsf_cj_part)release(kzsf_cj_part);
    if(kzsf_cj_original)release(kzsf_cj_original);
    kzsf_cj_part=kzsf_cj_original=kzsf_cj_owner=kzsf_cj_model=0;kzsf_cj_expires=0;
}
/* request: -2 diagnostic F11, -1 maintain, 0 restore, 1 select CJ. */
static void kzsf_custom_model_tick(uintptr_t owner,uintptr_t base,KzsfZoomRead read,void *opaque,uintptr_t tcb,KzsfCharacterType type,int request) {
    static int checked,enabled,key_down;static const char *preview_folder;
    if(!checked){const char *v=getenv("KZSF_CUSTOM_MODEL_PREVIEW");enabled=v && !strcmp(v,"1");preview_folder=getenv("KZSF_CUSTOM_MODEL_PROBE_DIR");checked=1;}
    int managed=request>=-1;
    const char *folder=managed ? kzsf_cj_folder() : preview_folder;
    if(request==-1 && !kzsf_cj_part)return;
    if((!managed && !enabled) || !folder || !*folder || !owner)return;
    KzsfCameraEngineContext engine;
    if(!kzsf_camera_engine_context(tcb,base,read,opaque,&engine) || engine.allocate_method!=base+0x4f67a0)return;
    uintptr_t model=0;if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) || !model)return;
    KzsfCustomRelease release=(KzsfCustomRelease)(base+0x330f20);
    if(kzsf_cj_part && (owner!=kzsf_cj_owner || model!=kzsf_cj_model)) {
        if(__atomic_load_n(&kzsf_character_render_part,__ATOMIC_ACQUIRE)==kzsf_cj_part)
            kzsf_character_render_select(0,0,-1);
        release(kzsf_cj_part);release(kzsf_cj_original);kzsf_cj_part=kzsf_cj_original=kzsf_cj_owner=kzsf_cj_model=0;
        fputs("KILLZONE_CUSTOM_MODEL_PREVIEW status=owner-changed state_refs=released\n",stderr);fflush(stderr);
    }
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    int key=foreground==GetCurrentProcessId() && !kzsfgpu_overlay_captures_input() && (GetAsyncKeyState(VK_F11)&0x8000)!=0;
    int pressed=managed ? request==1 : key && !key_down;key_down=key;
    KzsfCustomProbeContext context={base,read,opaque};const char *why=NULL;
    if(kzsf_cj_part && ((managed ? request==0 : pressed) || GetTickCount64()>=kzsf_cj_expires)) {
        /* A native state/weapon change may already have restored the body.
         * Only release our refs after confirming no slot retains this part. */
        kzsf_character_change_body_parts(owner,kzsf_cj_part,kzsf_cj_original,base,read,opaque,(KzsfCharacterChangePart)(base+0x667900),&why);
        uintptr_t array=0;int32_t count=0;int remaining=0;
        if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) ||
           !kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<1 || count>128 ||
           !kzsf_zoom_read(read,opaque,model,0x60,&array,8))return;
        for(int32_t i=0;i<count;i++){uintptr_t part=0;if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&part,8))return;remaining+=part==kzsf_cj_part;}
        if(!remaining) {
            if(__atomic_load_n(&kzsf_character_render_part,__ATOMIC_ACQUIRE)==kzsf_cj_part)
                kzsf_character_render_select(0,0,-1);
            release(kzsf_cj_part);release(kzsf_cj_original);kzsf_cj_part=kzsf_cj_original=kzsf_cj_owner=kzsf_cj_model=0;
            kzsf_observer_body_visibility(stderr,owner,base,read,opaque,tcb);
            fputs("KILLZONE_CUSTOM_MODEL_PREVIEW status=restored state_refs=released\n",stderr);fflush(stderr);
        }
        return;
    }
    if(kzsf_cj_part) {
        /* Native weapon/traversal changes can re-add the default body or
         * disable the replacement slot. The existing managed-model visibility
         * transaction keeps one CJ body visible and hides the exact default
         * body, preserving weapon/attachment records and native processing. */
        kzsf_observer_body_visibility(stderr,owner,base,read,opaque,tcb);
        return;
    }
    if(!pressed)return;
    char marker[4096];int length=snprintf(marker,sizeof(marker),"%s/preview.enable",folder);
    if(!managed && (length<0 || (size_t)length>=sizeof(marker) || GetFileAttributesA(marker)==INVALID_FILE_ATTRIBUTES)) {
        fputs("KILLZONE_CUSTOM_MODEL_PREVIEW status=blocked reason=review-marker\n",stderr);fflush(stderr);return;
    }
    uintptr_t old=0,part=kzsf_custom_model_load(folder,owner,&context,type,&old,managed);
    if(!part)return;
    if(!old || !kzsf_character_swap_compatible(old,part,base,read,opaque,type,&why)) {
        release(part);fprintf(stderr,"KILLZONE_CUSTOM_MODEL_PREVIEW status=refused reason=%s\n",why ? why : "original-part");fflush(stderr);return;
    }
    kzsf_material_native_retain_resource(old);
    uintptr_t array=0;int32_t count=0,body_slot=-1;
    if(kzsf_zoom_read(read,opaque,model,0x58,&count,4) && count>=1 && count<=128 &&
       kzsf_zoom_read(read,opaque,model,0x60,&array,8)) {
        for(int32_t i=0;i<count;i++){uintptr_t slot_part=0;
            if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&slot_part,8))break;
            if(slot_part==old){body_slot=i;break;}
        }
    }
    if(body_slot<0){release(old);release(part);return;}
    /* Hold both resources even if a multi-slot transaction partially fails;
     * the restoration branch checks and removes every remaining custom slot. */
    kzsf_cj_part=part;kzsf_cj_original=old;kzsf_cj_owner=owner;kzsf_cj_model=model;kzsf_cj_expires=managed ? ~(ULONGLONG)0 : GetTickCount64()+60000;
    /* Publish before changing slots, matching native replacement selection.
     * Otherwise the normal default-body maintenance re-enables Lucas over CJ. */
    kzsf_character_render_select_compatible(owner,model,kzsf_cj_part,body_slot);
    if(managed)__atomic_store_n(&kzsf_character_model_kind,KZSF_CHARACTER_MODEL_CJ,__ATOMIC_RELEASE);
    int changed=kzsf_character_change_body_parts(owner,old,part,base,read,opaque,(KzsfCharacterChangePart)(base+0x667900),&why);
    kzsf_zoom_read(read,opaque,owner,0x200,&kzsf_cj_model,8);
    if(!changed)kzsf_cj_expires=0;
    else kzsf_observer_body_visibility(stderr,owner,base,read,opaque,tcb);
    fprintf(stderr,"KILLZONE_CUSTOM_MODEL_PREVIEW status=%s reason=%s restore_seconds=%d\n",changed ? "attached" : "restore-pending",why ? why : "unknown",managed ? 0 : 60);fflush(stderr);
}
#endif
