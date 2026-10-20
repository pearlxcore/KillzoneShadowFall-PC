#ifndef KZSF_CHARACTER_RIG_AUDIT_H
#define KZSF_CHARACTER_RIG_AUDIT_H
#include "kzsf_character_mesh.h"

/* Read-only evidence for the native prefix-layout path in 70b5c0/5d79b0.
 * 5d7630 hashes joint names and signed parent indices: joints +30/+38,
 * stride18, String+00, parent short+10. This does NOT check scalar/IK
 * channels or initialize the additional pose bones, so never authorizes a swap. */
typedef struct {
    const char *reason;
    int32_t reference_count,candidate_count,joint;
} KzsfCharacterRigAudit;
static inline int kzsf_character_joint_name(uintptr_t skeleton,int32_t joint,KzsfZoomRead read,void *opaque,char name[129]) {
    uintptr_t array=0,text=0;int32_t count=0,length=0;
    if(!name || joint<0 || !kzsf_zoom_read(read,opaque,skeleton,0x30,&count,4) || count<=0 || count>512 || joint>=count ||
       !kzsf_zoom_read(read,opaque,skeleton,0x38,&array,8) || !array ||
       !kzsf_zoom_read(read,opaque,array,(size_t)joint*0x18,&text,8) || !text ||
       !kzsf_zoom_read(read,opaque,text,8,&length,4) || length<=0 || length>128 ||
       !kzsf_zoom_read(read,opaque,text,0x10,name,(size_t)length))return 0;
    name[length]=0;return length;
}
static inline int kzsf_character_joint_prefix(uintptr_t reference,uintptr_t candidate,
        KzsfZoomRead read,void *opaque,KzsfCharacterRigAudit *out) {
    KzsfCharacterRigAudit r={.reason="joint-header",.joint=-1};
    uintptr_t a=0,b=0;
#define KZSF_RIG_FAIL(label) do {r.reason=label;if(out)*out=r;return 0;} while(0)
    if(!read || !kzsf_zoom_read(read,opaque,reference,0x30,&r.reference_count,4) ||
       !kzsf_zoom_read(read,opaque,candidate,0x30,&r.candidate_count,4) ||
       r.reference_count<=0 || r.reference_count>512 ||
       r.candidate_count<r.reference_count || r.candidate_count>512 ||
       !kzsf_zoom_read(read,opaque,reference,0x38,&a,8) || !a ||
       !kzsf_zoom_read(read,opaque,candidate,0x38,&b,8) || !b)KZSF_RIG_FAIL("joint-header");
    for(int32_t i=0;i<r.reference_count;i++) {
        r.joint=i;uintptr_t an=0,bn=0;int16_t ap=0,bp=0;int32_t al=0,bl=0;
        unsigned char as[128],bs[128];
        if(!kzsf_zoom_read(read,opaque,a,(size_t)i*0x18,&an,8) || !an ||
           !kzsf_zoom_read(read,opaque,b,(size_t)i*0x18,&bn,8) || !bn ||
           !kzsf_zoom_read(read,opaque,a,(size_t)i*0x18+0x10,&ap,2) ||
           !kzsf_zoom_read(read,opaque,b,(size_t)i*0x18+0x10,&bp,2))KZSF_RIG_FAIL("joint-unreadable");
        if(ap!=bp)KZSF_RIG_FAIL("joint-parent-differs");
        if(!kzsf_zoom_read(read,opaque,an,8,&al,4) ||
           !kzsf_zoom_read(read,opaque,bn,8,&bl,4) || al<=0 || al>128 || bl<=0 || bl>128)
            KZSF_RIG_FAIL("joint-name-header");
        if(al!=bl)KZSF_RIG_FAIL("joint-name-length-differs");
        if(!kzsf_zoom_read(read,opaque,an,0x10,as,(size_t)al) ||
           !kzsf_zoom_read(read,opaque,bn,0x10,bs,(size_t)bl))KZSF_RIG_FAIL("joint-name-unreadable");
        if(memcmp(as,bs,(size_t)al))KZSF_RIG_FAIL("joint-name-differs");
    }
    r.reason="matching-joint-prefix";if(out)*out=r;
#undef KZSF_RIG_FAIL
    return 1;
}
#endif
