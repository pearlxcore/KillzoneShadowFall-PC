#include "../src/kzsf_timing.h"
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    size_t size=0x1221860;
    unsigned char *image=calloc(1,size);
    assert(image);
    const uint32_t sites[]={0x12074f8,0x1207508,0x1207684,0x1207690,0x1207b6c};
    const float original=1594000000.0f;
    uint64_t micro=UINT64_C(0x3f448e9d60000000);
    for (unsigned i=0;i<5;++i) memcpy(image+sites[i],&original,4);
    memcpy(image+0x11a5ab0,&micro,8);
    /* Reject an incomplete signature without partially patching earlier sites. */
    assert(!kzsf_timing_deadlines(image,size,3700000000.0));
    assert(!memcmp(image+sites[0],&original,4));
    memcpy(image+0x1221858,&micro,8);
    assert(!kzsf_timing_deadlines(image,size-1,3700000000.0));
    assert(!kzsf_timing_deadlines(image,size,NAN));
    assert(kzsf_timing_deadlines(image,size,3700000000.0)==7);
    float hz; double us;
    memcpy(&hz,image+sites[0],4); memcpy(&us,image+0x11a5ab0,8);
    assert(fabs((double)hz/3700000000.0-1)<1e-6);
    assert(fabs(3700000000.0*us-1000000.0)<0.001);
    assert(!kzsf_timing_deadlines(image,size,3700000000.0));
    free(image); puts("KZSF timing tests passed");
}
