#undef NDEBUG
#include "../src/kzsf_custom_model_manifest.h"
#include "../src/kzsf_custom_model_geometry.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    unsigned char data[84]={0};memcpy(data,"KZMD",4);
    uint32_t head[7]={1,98,1,1,1,123,0};memcpy(data+4,head,sizeof(head));
    uint32_t section[10]={3,3,0,168,38,5,6,0,0,0};memcpy(data+32,section,40);
    uint32_t texture[3]={16400,7,0};memcpy(data+72,texture,12);
    KzsfCustomModelManifest result;
    assert(kzsf_custom_model_manifest(data,sizeof(data),&result));
    assert(result.sections==1 && result.textures==1 && result.profile==123 && result.flags==1);
    assert(result.section[0].vertices==3 && result.texture[0].bytes==16400);
    assert(kzsf_custom_crc32(0,"123456789",9)==UINT32_C(0xcbf43926));
    assert(kzsf_custom_crc32(kzsf_custom_crc32(0,"1234",4),"56789",5)==UINT32_C(0xcbf43926));
    const unsigned offsets[]={4,8,12,16,20,28,32,36,40,44,48,60,72,80};
    const uint32_t invalid[]={2,97,17,0,2,1,0,4,1,167,37,1,15,1};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++) {
        unsigned char bad[84];memcpy(bad,data,sizeof(bad));memcpy(bad+offsets[i],&invalid[i],4);
        assert(!kzsf_custom_model_manifest(bad,sizeof(bad),&result));
    }
    for(size_t i=0;i<sizeof(data);i++)assert(!kzsf_custom_model_manifest(data,i,&result));
    unsigned char vertices[168]={0};uint32_t vh[5]={3,1,0x31,36,6};memcpy(vertices,vh,20);
    const unsigned char elements[24]={0,2,3,0,12,6,3,4,16,6,4,1,20,7,4,17,24,4,4,16,28,3,4,6};
    memcpy(vertices+20,elements,24);float lo[3],hi[3];
    assert(kzsf_custom_model_vertices(vertices,sizeof(vertices),3,lo,hi));
    vertices[80]=98;assert(!kzsf_custom_model_vertices(vertices,sizeof(vertices),3,lo,hi));vertices[80]=0;
    vertices[84]=200;vertices[85]=100;assert(!kzsf_custom_model_vertices(vertices,sizeof(vertices),3,lo,hi));vertices[84]=vertices[85]=0;
    vertices[87]=1;assert(!kzsf_custom_model_vertices(vertices,sizeof(vertices),3,lo,hi));vertices[87]=0;
    vertices[89]=0x7c;assert(!kzsf_custom_model_vertices(vertices,sizeof(vertices),3,lo,hi));vertices[89]=0;
    /* Native 8-index body layout: stride 44, 8 elements, 200 bytes for three vertices. */
    unsigned char wide[200]={0};uint32_t wh[5]={3,1,0x31,44,8};memcpy(wide,wh,20);
    const unsigned char eight[32]={0,2,3,0,12,6,3,4,16,6,4,1,20,7,4,17,24,7,4,19,28,4,4,16,32,4,4,18,36,3,4,6};
    memcpy(wide+20,eight,32);
    assert(kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));
    wide[68+27]=97;wide[68+34]=255;assert(kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));
    wide[68+28]=1;assert(!kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));wide[68+28]=0;   /* weights sum>255 */
    wide[68+35]=1;assert(!kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));wide[68+35]=0;   /* padding byte */
    wide[68+27]=98;assert(!kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));wide[68+27]=0;  /* 8th bone index */
    wide[68+37]=0x7c;assert(!kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));wide[68+37]=0;
    wide[20+12]=0;assert(!kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));wide[20+12]=20;  /* layout mismatch */
    assert(kzsf_custom_model_vertices(wide,sizeof(wide),3,lo,hi));
    assert(!kzsf_custom_model_vertices(wide,sizeof(wide),2,lo,hi));
    /* Manifest accepts the 8-index stream size and still rejects near misses. */
    unsigned char m8[84];memcpy(m8,data,sizeof(m8));uint32_t vb=200;memcpy(m8+44,&vb,4);
    assert(kzsf_custom_model_manifest(m8,sizeof(m8),&result));
    vb=199;memcpy(m8+44,&vb,4);assert(!kzsf_custom_model_manifest(m8,sizeof(m8),&result));
    assert(kzsf_custom_model_vertices(vertices,sizeof(vertices),3,lo,hi));
    puts("Custom model manifest tests passed");return 0;
}
