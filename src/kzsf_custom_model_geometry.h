#ifndef KZSF_CUSTOM_MODEL_GEOMETRY_H
#define KZSF_CUSTOM_MODEL_GEOMETRY_H
#include "kzsf_custom_vertex_upload.h"
/* Two accepted skinned layouts, both copied from captured native player streams:
 * - stride 36, 6 elements: position, normal, tangent, 4 bone indices, 3 explicit
 *   weights (+pad), half4 UV. Actual influence use depends on the material shader.
 * - stride 44, 8 elements (native body mesh 46): 8 bone indices in two sets,
 *   7 explicit weights in two sets (+pad), half4 UV. The implicit first weight is
 *   255 minus the seven stored bytes. */
static inline int kzsf_custom_model_vertices(const unsigned char *data,size_t bytes,uint32_t vertices,float lo[3],float hi[3]) {
    const unsigned char four[24]={0,2,3,0,12,6,3,4,16,6,4,1,20,7,4,17,24,4,4,16,28,3,4,6};
    const unsigned char eight[32]={0,2,3,0,12,6,3,4,16,6,4,1,20,7,4,17,24,7,4,19,28,4,4,16,32,4,4,18,36,3,4,6};
    KzsfCustomVertexLayout layout;
    if(!lo || !hi || !kzsf_custom_vertex_layout(data,bytes,&layout) || layout.vertices!=vertices)return 0;
    unsigned stride=layout.stride,bones,weights_at,explicit_weights,uv_at;
    if(stride==36 && layout.elements==6 && !memcmp(data+20,four,sizeof(four))){bones=4;weights_at=24;explicit_weights=3;uv_at=28;}
    else if(stride==44 && layout.elements==8 && !memcmp(data+20,eight,sizeof(eight))){bones=8;weights_at=28;explicit_weights=7;uv_at=36;}
    else return 0;
    for(unsigned j=0;j<3;j++){lo[j]=INFINITY;hi[j]=-INFINITY;}
    for(uint32_t i=0;i<vertices;i++) {
        const unsigned char *p=data+layout.data_offset+(size_t)i*stride;unsigned sum=0;
        for(unsigned j=0;j<explicit_weights;j++)sum+=p[weights_at+j];
        if(p[weights_at+explicit_weights] || sum>255)return 0;
        for(unsigned j=0;j<bones;j++)if(p[20+j]>=98)return 0;
        for(unsigned j=0;j<3;j++){float x;memcpy(&x,p+j*4,4);if(x<lo[j])lo[j]=x;if(x>hi[j])hi[j]=x;}
        for(unsigned j=0;j<4;j++){uint16_t h;memcpy(&h,p+uv_at+j*2,2);if((h&0x7c00)==0x7c00)return 0;}
    }
    return 1;
}
#endif
