#ifndef KZSF_CHARACTER_SWAP_H
#define KZSF_CHARACTER_SWAP_H
#include "kzsf_character_mesh.h"
#include "kzsf_character_native_query.h"
#include <math.h>

/* Conservative first prototype: same native skeleton object, exact binding
 * names/order and numerically agreeing Mat44 inverse binds. No bone remapping, physics or actor
 * resource changes. Matching this gate is not visual/gameplay validation. */
typedef struct {
    const char *reason;
    uintptr_t reference,candidate;
    int32_t bone;
    uint32_t stride,byte;
    unsigned char reference_byte,candidate_byte;
    float reference_value,candidate_value,delta,tolerance;
} KzsfCharacterBindingCheck;
static inline int kzsf_character_binding_compare(uintptr_t a,uintptr_t b,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterBindingCheck *check) {
    KzsfCharacterBindingCheck result={.reason="binding-header",.reference=a,.candidate=b,.bone=-1};
#define KZSF_BIND_FAIL(label) do {result.reason=label;if(check)*check=result;return 0;} while(0)
    if(check)*check=result;
    if(a==b)return a!=0;
    int32_t ac=0,bc=0;uintptr_t an=0,bn=0,am=0,bm=0;uint32_t stride=0;
    if(!kzsf_zoom_read(read,opaque,a,0x10,&ac,4) ||
       !kzsf_zoom_read(read,opaque,b,0x10,&bc,4) || ac!=bc || ac<=0 || ac>512 ||
       !kzsf_zoom_read(read,opaque,a,0x18,&an,8) ||
       !kzsf_zoom_read(read,opaque,b,0x18,&bn,8) ||
       !kzsf_zoom_read(read,opaque,a,0x30,&am,8) ||
       !kzsf_zoom_read(read,opaque,b,0x30,&bm,8))KZSF_BIND_FAIL("binding-header");
    if(!kzsf_zoom_read(read,opaque,base,0x3b40f84,&stride,4))KZSF_BIND_FAIL("binding-stride-unreadable");
    result.stride=stride;
    /* Native registration317ac9 and attributes14f5ac0 identify Mat44 as
     * exactly four float4 columns at00/10/20/30, size40. No guessed stride. */
    if(stride!=64)KZSF_BIND_FAIL("binding-stride-invalid");
    for(int32_t i=0;i<ac;i++) {
        result.bone=i;
        uintptr_t x=0,y=0;int32_t xl=0,yl=0;
        if(!kzsf_zoom_read(read,opaque,an,(size_t)i*8,&x,8) ||
           !kzsf_zoom_read(read,opaque,bn,(size_t)i*8,&y,8) || !x || !y)KZSF_BIND_FAIL("bone-name-pointer");
        if(x!=y) {
            unsigned char xs[128],ys[128];
            if(!kzsf_zoom_read(read,opaque,x,8,&xl,4) ||
               !kzsf_zoom_read(read,opaque,y,8,&yl,4))KZSF_BIND_FAIL("bone-name-header");
            if(xl!=yl)KZSF_BIND_FAIL("bone-name-length-differs");
            if(xl<0 || xl>128)KZSF_BIND_FAIL("bone-name-length-invalid");
            if(
               !kzsf_zoom_read(read,opaque,x,0x10,xs,(size_t)xl) ||
               !kzsf_zoom_read(read,opaque,y,0x10,ys,(size_t)yl))KZSF_BIND_FAIL("bone-name-unreadable");
            if(memcmp(xs,ys,(size_t)xl))KZSF_BIND_FAIL("bone-name-differs");
        }
        unsigned char xm[128],ym[128];
        if(!kzsf_zoom_read(read,opaque,am,(size_t)i*stride,xm,stride) ||
           !kzsf_zoom_read(read,opaque,bm,(size_t)i*stride,ym,stride))KZSF_BIND_FAIL("inverse-bind-unreadable");
        for(uint32_t j=0;j<stride;j+=4) {
            float x,y;memcpy(&x,xm+j,4);memcpy(&y,ym+j,4);
            result.byte=j;result.reference_byte=xm[j];result.candidate_byte=ym[j];
            result.reference_value=x;result.candidate_value=y;
            if(!isfinite(x) || !isfinite(y))KZSF_BIND_FAIL("inverse-bind-nonfinite");
            result.delta=fabsf(x-y);
            result.tolerance=1e-6f+1e-6f*fmaxf(fabsf(x),fabsf(y));
            if(result.delta>result.tolerance)KZSF_BIND_FAIL("inverse-bind-differs");
        }
    }
    result.reason="matching-bindings";if(check)*check=result;
#undef KZSF_BIND_FAIL
    return 1;
}
static inline int kzsf_character_swap_compatible_ex(uintptr_t old_part,uintptr_t new_part,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,const char **why,KzsfCharacterBindingCheck *check) {
    KzsfCharacterPart old,new;KzsfCharacterMeshTree a,b;
    if(why)*why="part-layout";
    if(!kzsf_character_part_read(old_part,base,read,opaque,type,&old) ||
       !kzsf_character_part_read(new_part,base,read,opaque,type,&new) ||
       !old.is_skinned || !new.is_skinned || old.physics || new.physics)return 0;
    if(why)*why="mesh-layout";
    if(!kzsf_character_mesh_read(old.mesh,base,read,opaque,type,&a) ||
       !kzsf_character_mesh_read(new.mesh,base,read,opaque,type,&b) ||
       !a.count || !b.count || a.unknown_count || b.unknown_count)return 0;
    KzsfCharacterMeshLeaf *ref=&a.leaves[0];
    for(int pass=0;pass<2;pass++) {
        KzsfCharacterMeshTree *tree=pass ? &b : &a;
        for(int32_t i=0;i<tree->count;i++) {
            KzsfCharacterMeshLeaf *leaf=&tree->leaves[i];
            if(why)*why="skeleton-mismatch";
            if(leaf->skeleton!=ref->skeleton || leaf->animator_hash!=ref->animator_hash ||
               leaf->layout_hash!=ref->layout_hash || leaf->binding_count!=ref->binding_count)return 0;
            if(why)*why="binding-mismatch";
            KzsfCharacterBindingCheck detail;
            if(!kzsf_character_binding_compare(ref->bindings,leaf->bindings,base,read,opaque,&detail)) {
                if(why)*why=detail.reason;if(check)*check=detail;return 0;
            }
        }
    }
    if(why)*why="matching-bindings";return 1;
}
static inline int kzsf_character_swap_compatible(uintptr_t old_part,uintptr_t new_part,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,const char **why) {
    return kzsf_character_swap_compatible_ex(old_part,new_part,base,read,opaque,type,why,NULL);
}
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterChangePart)(const uintptr_t *,uintptr_t);
static inline int kzsf_character_change_part_one(uintptr_t owner,uintptr_t old_part,uintptr_t new_part,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterChangePart change,const char **why,int unique) {
    static const unsigned char guard[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
        0x53,0x48,0x81,0xec,0xc8,0x01,0x00,0x00};
    unsigned char code[sizeof(guard)];uintptr_t model=0,parent=0,array=0;int32_t count=0,slot=-1;
    if(why)*why="native-part-guard";
    if(!change || !kzsf_zoom_read(read,opaque,base,0x667900,code,sizeof(code)) || memcmp(code,guard,sizeof(code)))return 0;
    if(why)*why="model-ownership";
    if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) ||
       !kzsf_zoom_read(read,opaque,model,0x38,&parent,8) || parent!=owner ||
       !kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<=0 || count>128 ||
       !kzsf_zoom_read(read,opaque,model,0x60,&array,8))return 0;
    for(int32_t i=0;i<count;i++) {
        uintptr_t p=0;if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&p,8))return 0;
        if(p==old_part){if(slot>=0){if(unique)return 0;}else slot=i;}
    }
    if(why)*why="old-part-not-unique";
    if(slot<0)return 0;
    /* 667900 only consumes action+18/+20, verified from complete function.
     * Calls native model transactions and retention; no fake vtable invoked. */
    uintptr_t action[5]={0,0,0,old_part,new_part};
    change(action,owner);
    uintptr_t after=0;
    if(why)*why="native-change-unconfirmed";
    if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) ||
       !kzsf_zoom_read(read,opaque,model,0x60,&array,8) ||
       !kzsf_zoom_read(read,opaque,array,(size_t)slot*0xf0+0xc8,&after,8) || after!=new_part)return 0;
    if(why)*why="native-change-confirmed";return 1;
}
static inline int kzsf_character_change_part(uintptr_t owner,uintptr_t old_part,uintptr_t new_part,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterChangePart change,const char **why) {
    return kzsf_character_change_part_one(owner,old_part,new_part,base,read,opaque,change,why,1);
}
/* Experimental appearance restoration/selection: change every occurrence of
 * this exact owned body resource. Preserve the native descriptor/visibility
 * transaction for each slot; never rewrite resource pointers directly. */
static inline int kzsf_character_change_body_parts(uintptr_t owner,uintptr_t old_part,uintptr_t new_part,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterChangePart change,const char **why) {
    if(!old_part || !new_part || old_part==new_part)return 0;
    int changed=0,previous_count=129;
    for(int attempt=0;attempt<129;attempt++) {
        uintptr_t model=0,array=0;int32_t count=0,remaining=0;
        if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) ||
           !kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<1 || count>128 ||
           !kzsf_zoom_read(read,opaque,model,0x60,&array,8))return 0;
        for(int32_t i=0;i<count;i++) {
            uintptr_t part=0;
            if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&part,8))return 0;
            remaining+=part==old_part;
        }
        if(!remaining){if(why)*why="all-body-parts-changed";return changed>0;}
        if(remaining>=previous_count){if(why)*why="body-change-no-progress";return 0;}
        previous_count=remaining;
        if(!kzsf_character_change_part_one(owner,old_part,new_part,base,read,opaque,change,why,0))return 0;
        changed++;
    }
    if(why)*why="body-change-limit";return 0;
}
#endif
