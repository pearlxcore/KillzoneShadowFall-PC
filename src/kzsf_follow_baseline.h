#ifndef KZSF_FOLLOW_BASELINE_H
#define KZSF_FOLLOW_BASELINE_H
#include "kzsf_weapon_zoom.h"
#include <string.h>
#include <math.h>

/* Necessary observed campaign baseline, NOT permission to override a camera.
 * Caller supplies exact type decoding. No guest calls, retention or writes.
 * Two agreeing reads reduce races; they do not prevent ABA or make it atomic. */
typedef uintptr_t (*KzsfFollowType)(uintptr_t,uintptr_t,KzsfZoomRead,void *);
typedef struct {
    uintptr_t view,render,user,owner,source,source_rep,controller,state,stack;
    uintptr_t camera_owner,ladder_controller,ladder_override,retained_source;
    uintptr_t climb_layer,climb_resource,climb_effect;
    uintptr_t combat_component,combat_layer,combat_resource,combat_effect,combat_transition;
    uintptr_t throwable,throwable_inventory;
    uintptr_t unarmed,unarmed_inventory;
    uintptr_t knife,knife_inventory;
    uintptr_t outgoing;float blend;
    uintptr_t handoff_owner,handoff_controller;
    uintptr_t pose_owner;
    KzsfWeaponZoom zoom;
} KzsfFollowBaseline;
/* Shared InteractiveEntity links, verified by a74960 and a6c710. During
 * prelude/return the Soldier's use-location or view's outgoing camera owns
 * the link; a detached (+598==0) object is allowed there, a foreign actor is
 * never allowed. Current object-owned cameras require +598 below. */
static inline int kzsf_follow_interaction_link(uintptr_t object,uintptr_t actor,uintptr_t camera,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfFollowType type,uintptr_t *controller) {
    uintptr_t t=type(object,base,read,opaque),handle=0,c=0,parent=0,source=0;
    int ladder=t==base+0x3c9a390;
    if(!ladder && t!=base+0x3c99180 && t!=base+0x3c9e7c0 && t!=base+0x3c9c150)return 0;
    if(actor>UINTPTR_MAX-0x48 ||
       !kzsf_zoom_read(read,opaque,object,0x598,&handle,8) || (handle && handle!=actor+0x48) ||
       !kzsf_zoom_read(read,opaque,object,0x290,&c,8) ||
       !kzsf_zoom_read(read,opaque,c,0x40,&parent,8) || parent!=object)return 0;
    if(ladder ? type(c,base,read,opaque)!=base+0x3c9a800 :
       (type(c,base,read,opaque)!=base+0x3c99960 ||
        !kzsf_zoom_method(read,opaque,c,0x108,base+0xa6c710) ||
        !kzsf_zoom_method(read,opaque,c,0x110,base+0xa6c8f0)))return 0;
    if(camera && (!kzsf_zoom_read(read,opaque,c,0x28,&source,8) || source!=camera))return 0;
    *controller=c;return 1;
}
/* Exact native traversal classes verified from registration and accessors. */
static inline int kzsf_follow_supported_state(uintptr_t state_type,uintptr_t base) {
    static const uint32_t states[]={0x3cad168, /* joystick walk */
        0x3ca91b0, /* fall */ 0x3caa970, /* climb */ 0x3cada28, /* climb ledge */
        0x3cabd20, /* vault */ 0x3cad4e8, /* slide */ 0x3ca8d50, /* running slide */
        0x3ca9290, /* zipline: observed normal controller, no override/layers */
        0x3cad788}; /* ZeroGravity: Chapter3 live owned FP camera, ordinary joystick controller */
    if(state_type<base)return 0;
    for(unsigned i=0;i<sizeof(states)/sizeof(*states);i++)if(state_type-base==states[i])return 1;
    return 0;
}
/* Appearance transactions and full-body recovery are supported during
 * ordinary walking and freely controlled zero gravity. Camera follow alone
 * also supports traversal states, which must not authorize a model swap. */
static inline int kzsf_follow_model_state(uintptr_t state_type,uintptr_t base) {
    if(state_type<base)return 0;
    uintptr_t rva=state_type-base;
    return rva==0x3cad168 || rva==0x3cad788;
}

static inline int kzsf_follow_baseline_collect(uintptr_t view,uintptr_t render,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfFollowType type,KzsfFollowBaseline *out,const char **why) {
    KzsfFollowBaseline b;memset(&b,0,sizeof(b));b.view=view;b.render=render;
    uintptr_t view_render=0,user_view=0,outgoing=0,top=0,parent=0,override=0;
    int32_t count=-1,layers=-1;float blend=0;
#define KZSF_BASELINE_REFUSE(reason) do { if(why)*why=reason;return 0; } while(0)
#define KZSF_BASELINE_READ(p,offset,value) \
    do { if(!kzsf_zoom_read(read,opaque,p,offset,&value,sizeof(value))) \
        KZSF_BASELINE_REFUSE("unreadable"); } while(0)
    if(!read || !type || !out || base>UINTPTR_MAX-0x3ffffff)
        KZSF_BASELINE_REFUSE("invalid-input");
    if(type(view,base,read,opaque)!=base+0x3cce7f8 ||
       type(render,base,read,opaque)!=base+0x3c26d60)KZSF_BASELINE_REFUSE("view-type");
    KZSF_BASELINE_READ(view,0x10,b.user);KZSF_BASELINE_READ(view,0x18,b.owner);
    KZSF_BASELINE_READ(view,0x20,view_render);KZSF_BASELINE_READ(view,0x38,b.source);
    KZSF_BASELINE_READ(view,0x50,outgoing);KZSF_BASELINE_READ(view,0xf0,blend);
    b.outgoing=outgoing;b.blend=blend;
    if(view_render!=render)KZSF_BASELINE_REFUSE("render-mismatch");
    b.camera_owner=b.owner;
    if(type(b.user,base,read,opaque)!=base+0x3c73920)KZSF_BASELINE_REFUSE("owner-type");
    int ladder=type(b.camera_owner,base,read,opaque)==base+0x3c9a390;
    uintptr_t owner_type=type(b.camera_owner,base,read,opaque);
    int play_animation=owner_type==base+0x3c9c150;
    int interaction=ladder || owner_type==base+0x3c99180 || owner_type==base+0x3c9e7c0 || play_animation;
    if(interaction) {
        /* InteractiveEntity user getter a74960 follows +598 actor interface.
         * Ladder controller inherits ab4330: +40 entity, +28 camera interface. */
        uintptr_t actor=0,controlled=0,camera=0;
        KZSF_BASELINE_READ(b.camera_owner,0x598,actor);
        if(actor<=0x48)KZSF_BASELINE_REFUSE("ladder-actor");
        b.owner=actor-0x48;
        KZSF_BASELINE_READ(b.camera_owner,0x290,b.ladder_controller);
        if(ladder ? type(b.ladder_controller,base,read,opaque)!=base+0x3c9a800 :
           (type(b.ladder_controller,base,read,opaque)!=base+0x3c99960 ||
            !kzsf_zoom_method(read,opaque,b.ladder_controller,0x108,base+0xa6c710) ||
            !kzsf_zoom_method(read,opaque,b.ladder_controller,0x110,base+0xa6c8f0)))
            KZSF_BASELINE_REFUSE("ladder-controller");
        KZSF_BASELINE_READ(b.ladder_controller,0x40,controlled);
        KZSF_BASELINE_READ(b.ladder_controller,0x28,camera);
        if(controlled!=b.camera_owner || camera!=b.source)
            KZSF_BASELINE_REFUSE("ladder-camera-ownership");
    }
    if(type(b.owner,base,read,opaque)!=base+0x3ca98b0)KZSF_BASELINE_REFUSE("owner-type");
    if(!interaction && (outgoing || blend!=1.0f)) {
        uintptr_t outgoing_type=outgoing>0x48 ? type(outgoing-0x48,base,read,opaque) : 0;
        if(!outgoing || !isfinite(blend) || blend<0 || blend>1 ||
           (outgoing_type!=base+0x3c8ebd0 && outgoing_type!=base+0x3c288a8))
            KZSF_BASELINE_REFUSE("source-transition");
        KZSF_BASELINE_READ(outgoing-0x48,0x1e0,b.handoff_owner);
        if(!kzsf_follow_interaction_link(b.handoff_owner,b.owner,outgoing,base,read,opaque,type,
                                       &b.handoff_controller))KZSF_BASELINE_REFUSE("source-transition");
    }
    KZSF_BASELINE_READ(b.user,0xc0,user_view);KZSF_BASELINE_READ(b.user,0xc8,count);
    if(user_view!=view)KZSF_BASELINE_REFUSE("user-view-mismatch");
    /* Restrict to the single-camera stack observed in campaign samples. */
    if(count!=(interaction ? 2 : 1))KZSF_BASELINE_REFUSE("camera-stack-count");
    KZSF_BASELINE_READ(b.user,0xd0,b.stack);
    KZSF_BASELINE_READ(b.stack,interaction ? 0x18 : 0,top);
    if(interaction) {
        uintptr_t retained_parent=0,retained_rep=0;
        KZSF_BASELINE_READ(b.stack,0,b.retained_source);
        if(b.retained_source<=0x48 || b.retained_source==top ||
           type(b.retained_source-0x48,base,read,opaque)!=base+0x3c8ebd0)
            KZSF_BASELINE_REFUSE("ladder-retained-source");
        KZSF_BASELINE_READ(b.retained_source-0x48,0x1e0,retained_parent);
        KZSF_BASELINE_READ(b.retained_source-0x48,0x1d8,retained_rep);
        if(retained_parent!=b.owner || type(retained_rep,base,read,opaque)!=base+0x3c95d88)
            KZSF_BASELINE_REFUSE("ladder-retained-owner");
        if(outgoing ? (outgoing!=b.retained_source || !isfinite(blend) || blend<0 || blend>1) : blend!=1.0f)
            KZSF_BASELINE_REFUSE("source-transition");
    }
    if(top!=b.source || top<=0x48)KZSF_BASELINE_REFUSE("source-stack-mismatch");
    uintptr_t entity=top-0x48;
    uintptr_t source_type=type(entity,base,read,opaque);
    int generic_camera=play_animation && source_type==base+0x3c288a8;
    if(source_type!=base+0x3c8ebd0 && !generic_camera)KZSF_BASELINE_REFUSE("source-type");
    KZSF_BASELINE_READ(entity,0x1e0,parent);KZSF_BASELINE_READ(entity,0x1d8,b.source_rep);
    if(parent!=b.camera_owner || type(b.source_rep,base,read,opaque)!=base+
       (generic_camera ? 0x3c28988 : 0x3c95d88))
        KZSF_BASELINE_REFUSE("source-parent-or-rep");
    KZSF_BASELINE_READ(b.owner,0x290,b.controller);KZSF_BASELINE_READ(b.owner,0x900,b.state);
    KZSF_BASELINE_READ(b.owner,0x8b8,override);
    uintptr_t state_type=type(b.state,base,read,opaque);
    int owned_pose=(interaction || b.handoff_owner) && state_type==base+0x3cac7a8;
    if(owned_pose) {
        uintptr_t state_actor=0;
        /* 9b09e0 resolves the exact pose override's InteractiveEntity at+40. */
        KZSF_BASELINE_READ(b.state,0x40,b.pose_owner);
        KZSF_BASELINE_READ(b.state,0x28,state_actor);
        if(b.pose_owner!=(interaction ? b.camera_owner : b.handoff_owner) || state_actor!=b.owner)
            KZSF_BASELINE_REFUSE("interaction-pose-ownership");
    }
    if(interaction && (override || !owned_pose)) {
        uintptr_t associated=0;b.ladder_override=override;
        if(type(override,base,read,opaque)!=base+0x3c74dc0)
            KZSF_BASELINE_REFUSE("ladder-use-location");
        KZSF_BASELINE_READ(override,0x38,associated);
        if(associated!=b.camera_owner)KZSF_BASELINE_REFUSE("ladder-use-owner");
    } else if(override) {
        if(type(override,base,read,opaque)!=base+0x3c74dc0)
            KZSF_BASELINE_REFUSE("player-override");
        KZSF_BASELINE_READ(override,0x38,b.handoff_owner);
        if(!kzsf_follow_interaction_link(b.handoff_owner,b.owner,0,base,read,opaque,type,
                                       &b.handoff_controller))KZSF_BASELINE_REFUSE("player-override");
        b.ladder_override=override;
    }
    /* The interaction controller and reciprocal pose links already establish
     * ownership here. Soldier+290 is the ordinary input controller; native
     * animation control need not retain its walking-controller classification.
     * We neither call nor alter that input controller. */
    if(!owned_pose && type(b.controller,base,read,opaque)!=base+0x3caa190)
        KZSF_BASELINE_REFUSE("controller-type");
    if(!owned_pose && !kzsf_follow_supported_state(state_type,base))KZSF_BASELINE_REFUSE("player-state");
    if(!kzsf_weapon_zoom_read(b.owner,base,read,opaque,&b.zoom)) {
        /* Grenades and the exact empty-inventory item have no portable gun
         * mode/zoom layout. Unknown items still refuse; absence is not ADS. */
        uintptr_t handle=0,again=0,inventory=0,item_type=0;
        KZSF_BASELINE_READ(b.owner,0xa48,inventory);
        KZSF_BASELINE_READ(inventory,0x68,handle);
        if(handle>0x48)item_type=type(handle-0x48,base,read,opaque);
        if(item_type!=base+0x3cb0c40 && item_type!=base+0x3cb15e0 && item_type!=base+0x3cafa90)
            KZSF_BASELINE_REFUSE("zoom-unknown");
        if(item_type==base+0x3cb15e0) {b.unarmed=handle-0x48;b.unarmed_inventory=inventory;}
        else if(item_type==base+0x3cafa90) {b.knife=handle-0x48;b.knife_inventory=inventory;}
        else {b.throwable=handle-0x48;b.throwable_inventory=inventory;}
        KZSF_BASELINE_READ(b.owner,0xa48,again);
        if(again!=inventory)KZSF_BASELINE_REFUSE("changed-during-sample");
        KZSF_BASELINE_READ(inventory,0x68,again);
        if(again!=handle)KZSF_BASELINE_REFUSE("changed-during-sample");
    }
    if(b.zoom.zoom_index!=0)KZSF_BASELINE_REFUSE("zoom-selected");
    if(generic_camera)layers=0; /* FP camera layers do not apply to CameraEntity. */
    else KZSF_BASELINE_READ(entity,0x278,layers);
    if(interaction && layers!=0)KZSF_BASELINE_REFUSE("ladder-camera-layers");
    if(layers!=0) {
        /* bc98c0: ClimbLedgeState retains layer+18 at state+138;
         * resource=*(state+30)+78, effect vtable1653360, priority1. */
        uintptr_t array=0,handle=0,state_resource=0,state_owner=0,table=0,resource=0,effect_table=0;
        int32_t key=-1,direction=-1,special=-1;
        if(layers!=1)
            KZSF_BASELINE_REFUSE("camera-layers-present-or-invalid");
        KZSF_BASELINE_READ(entity,0x280,array);KZSF_BASELINE_READ(array,0,b.combat_layer);
        KZSF_BASELINE_READ(b.combat_layer,0x2c,key);
        if(key==6) {
            /* f44de0: target is Soldier interface+48, whose +8b0 is
             * CloseCombatComponent; hence direct Soldier+8f8. Validate
             * reciprocal ownership and the exact retained camera handle. */
            uint8_t active=0;
            KZSF_BASELINE_READ(b.owner,0x8f8,b.combat_component);
            if(type(b.combat_component,base,read,opaque)!=base+0x3ced350)
                KZSF_BASELINE_REFUSE("combat-component-type");
            KZSF_BASELINE_READ(b.combat_component,0x38,state_owner);
            KZSF_BASELINE_READ(b.combat_component,0x88,handle);
            KZSF_BASELINE_READ(b.combat_component,0x20,state_resource);
            KZSF_BASELINE_READ(b.combat_component,0xdd,active);
            KZSF_BASELINE_READ(b.combat_layer,0x28,direction);
            KZSF_BASELINE_READ(state_resource,0x78,b.combat_resource);
            KZSF_BASELINE_READ(state_resource,0x80,b.combat_transition);
            /* f3f9e0 clears attacker+dd, then invokes layer virtual58(1)
             * (675050 sets direction+28) while retaining component+88.
             * The owned reverse/fade layer remains valid after execution. */
            if(state_owner!=b.owner || !b.combat_resource || active>1 ||
               (!active && direction!=1) ||
               b.combat_layer>UINTPTR_MAX-0x18 || handle!=b.combat_layer+0x18)
                KZSF_BASELINE_REFUSE("combat-layer-ownership");
            KZSF_BASELINE_READ(b.combat_layer,0,table);
            KZSF_BASELINE_READ(b.combat_layer,0x28,direction);
            KZSF_BASELINE_READ(b.combat_layer,0x3c,special);
            KZSF_BASELINE_READ(b.combat_layer,0x70,resource);
            KZSF_BASELINE_READ(b.combat_layer,0x50,b.combat_effect);
            KZSF_BASELINE_READ(b.combat_effect,0,effect_table);
            if(table!=base+0x1647070 || (direction!=0 && direction!=1) || special!=0 ||
               resource!=b.combat_resource || effect_table!=base+
                   (b.combat_transition ? 0x16534c0 : 0x1653360))
                KZSF_BASELINE_REFUSE("combat-layer-layout");
            if(b.combat_transition) {
                uintptr_t transition=0;KZSF_BASELINE_READ(b.combat_effect,0x18,transition);
                if(transition!=b.combat_transition)KZSF_BASELINE_REFUSE("combat-effect-resource");
            }
        } else {
        b.combat_layer=0;
        if(type(b.state,base,read,opaque)!=base+0x3cada28)
            KZSF_BASELINE_REFUSE("camera-layers-present-or-invalid");
        KZSF_BASELINE_READ(entity,0x280,array);KZSF_BASELINE_READ(array,0,b.climb_layer);
        KZSF_BASELINE_READ(b.state,0x138,handle);KZSF_BASELINE_READ(b.state,0x28,state_owner);
        KZSF_BASELINE_READ(b.state,0x30,state_resource);
        KZSF_BASELINE_READ(state_resource,0x78,b.climb_resource);
        if(!b.climb_layer || b.climb_layer>UINTPTR_MAX-0x18 || handle!=b.climb_layer+0x18 ||
           state_owner!=b.owner || !b.climb_resource)KZSF_BASELINE_REFUSE("climb-layer-ownership");
        KZSF_BASELINE_READ(b.climb_layer,0,table);KZSF_BASELINE_READ(b.climb_layer,0x2c,key);
        KZSF_BASELINE_READ(b.climb_layer,0x28,direction);KZSF_BASELINE_READ(b.climb_layer,0x3c,special);
        KZSF_BASELINE_READ(b.climb_layer,0x70,resource);KZSF_BASELINE_READ(b.climb_layer,0x50,b.climb_effect);
        KZSF_BASELINE_READ(b.climb_effect,0,effect_table);
        if(table!=base+0x1647070 || key!=1 || (direction!=0 && direction!=1) || special!=0 ||
           resource!=b.climb_resource || effect_table!=base+0x1653360)
            KZSF_BASELINE_REFUSE("climb-layer-layout");
        }
    }
    *out=b;if(why)*why="matches-baseline";
#undef KZSF_BASELINE_READ
#undef KZSF_BASELINE_REFUSE
    return 1;
}
static inline int kzsf_follow_baseline_read(uintptr_t view,uintptr_t render,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfFollowType type,KzsfFollowBaseline *out,const char **why) {
    KzsfFollowBaseline a,b;
    if(!out){if(why)*why="invalid-input";return 0;}
    if(!kzsf_follow_baseline_collect(view,render,base,read,opaque,type,&a,why) ||
       !kzsf_follow_baseline_collect(view,render,base,read,opaque,type,&b,why))return 0;
    if(a.view!=b.view || a.render!=b.render || a.user!=b.user || a.owner!=b.owner ||
       a.source!=b.source || a.source_rep!=b.source_rep || a.controller!=b.controller ||
       a.state!=b.state || a.stack!=b.stack || a.zoom.weapon!=b.zoom.weapon ||
       a.camera_owner!=b.camera_owner || a.ladder_controller!=b.ladder_controller ||
       a.ladder_override!=b.ladder_override || a.retained_source!=b.retained_source ||
       a.throwable!=b.throwable || a.throwable_inventory!=b.throwable_inventory ||
       a.unarmed!=b.unarmed || a.unarmed_inventory!=b.unarmed_inventory ||
       a.knife!=b.knife || a.knife_inventory!=b.knife_inventory ||
       a.outgoing!=b.outgoing || a.blend!=b.blend ||
       a.handoff_owner!=b.handoff_owner || a.handoff_controller!=b.handoff_controller ||
       a.pose_owner!=b.pose_owner ||
       a.climb_layer!=b.climb_layer || a.climb_resource!=b.climb_resource || a.climb_effect!=b.climb_effect ||
       a.combat_component!=b.combat_component || a.combat_layer!=b.combat_layer ||
       a.combat_resource!=b.combat_resource || a.combat_effect!=b.combat_effect ||
       a.combat_transition!=b.combat_transition ||
       a.zoom.mode_interface!=b.zoom.mode_interface || a.zoom.zoom_interface!=b.zoom.zoom_interface ||
       a.zoom.mode_index!=b.zoom.mode_index || a.zoom.mode_count!=b.zoom.mode_count ||
       a.zoom.zoom_index!=b.zoom.zoom_index || a.zoom.zoom_count!=b.zoom.zoom_count) {
        if(why)*why="changed-during-sample";return 0;
    }
    *out=a;return 1;
}
#endif
