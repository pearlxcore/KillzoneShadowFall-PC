/* CUSA00190 01.81 post-process feature-mask switches: bloom (bit 2) and film
 * grain (bit 3) beside the existing motion blur (bit 0) and DOF (bit 1).
 * Validated against the real linked image, not a synthetic buffer. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/kzsf_motion_blur.h"
#include "../src/kzsf_depth_of_field.h"
#include "../src/kzsf_post_effects.h"

static unsigned char *load_image(unsigned char **storage,size_t *image_size) {
    FILE *file=fopen("out/boot-linked.bin","rb");
    assert(file);
    unsigned char magic[8]; uint64_t size=0;
    assert(fread(magic,1,8,file)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&size,8,1,file)==1 && size>KZSF_FF_FLAGS && size<512*1024*1024);
    assert(!_fseeki64(file,-(long long)size,SEEK_END));
    unsigned char *image=malloc((size_t)size);
    assert(image);
    assert(fread(image,1,(size_t)size,file)==(size_t)size);
    fclose(file);
    *storage=image; *image_size=(size_t)size;
    return image;
}
static uint32_t mask_of(const unsigned char *image) {
    uint32_t flags; memcpy(&flags,image+KZSF_FF_FLAGS,4); return flags;
}
int main(void) {
    unsigned char *storage=NULL; size_t size=0;
    unsigned char *base=load_image(&storage,&size);
    /* The shipped image must carry the all-features-on layout. */
    assert(mask_of(base)==0xfffu);
    /* Snapshot the bytes around the mask so we can prove nothing else is written. */
    unsigned char around[12];
    memcpy(around,base+KZSF_FF_FLAGS-4,12);

    unsigned char *image=malloc(size);
    assert(image);

    /* Wrong code: a corrupted guard byte refuses the patch and leaves the mask alone. */
    memcpy(image,base,size);
    image[KZSF_GRAIN_GUARD]^=0x01;
    uint32_t before=0xdead,after=0xdead;
    assert(!kzsf_disable_film_grain(image,size,&before,&after));
    assert(mask_of(image)==0xfffu && before==0xdead && after==0xdead);
    memcpy(image,base,size);
    image[KZSF_BLOOM_GUARD+7]^=0x01;
    assert(!kzsf_disable_bloom(image,size,&before,&after));
    assert(mask_of(image)==0xfffu);
    /* A changed branch target must also refuse the patch. */
    memcpy(image,base,size);
    image[KZSF_BLOOM_GUARD+19]^=0x01;
    assert(!kzsf_disable_bloom(image,size,&before,&after));
    assert(mask_of(image)==0xfffu);
    /* Wrong image: a short buffer cannot reach the guard or the mask. */
    assert(!kzsf_disable_film_grain(image,16,&before,&after));
    assert(!kzsf_disable_bloom(NULL,size,&before,&after));

    /* Unknown mask layout: bits 4..11 must still be set, otherwise refuse. */
    memcpy(image,base,size);
    uint32_t unknown=0x00fu;
    memcpy(image+KZSF_FF_FLAGS,&unknown,4);
    assert(!kzsf_disable_film_grain(image,size,&before,&after));
    assert(!kzsf_disable_bloom(image,size,&before,&after));
    assert(mask_of(image)==unknown);
    /* Bits above the known 12-bit layout must not pass the guard. */
    for(unsigned bit=12;bit<32;++bit) {
        unknown=0xfffu|(1u<<bit);
        memcpy(image+KZSF_FF_FLAGS,&unknown,4);
        assert(!kzsf_disable_film_grain(image,size,&before,&after));
        assert(!kzsf_disable_bloom(image,size,&before,&after));
        assert(mask_of(image)==unknown);
    }
    /* All supported combinations of the four controlled bits remain valid. */
    for(unsigned low=0;low<16;++low) {
        uint32_t flags=0xff0u|low;
        memcpy(image+KZSF_FF_FLAGS,&flags,4);
        assert(kzsf_disable_bloom(image,size,&before,&after));
        assert(after==(flags&~4u));
        assert(kzsf_disable_film_grain(image,size,&before,&after));
        assert(after==(flags&~12u));
    }

    /* The documented order: the strict motion-blur/DOF guards first, then these. */
    memcpy(image,base,size);
    assert(kzsf_disable_motion_blur(image,size,&before,&after));
    assert(before==0xfffu && after==0xffeu);
    assert(kzsf_disable_depth_of_field(image,size,&before,&after));
    assert(before==0xffeu && after==0xffcu);
    assert(kzsf_disable_film_grain(image,size,&before,&after));
    assert(before==0xffcu && after==0xff4u);
    assert(kzsf_disable_bloom(image,size,&before,&after));
    assert(before==0xff4u && after==0xff0u);
    assert(mask_of(image)==0xff0u);
    /* Only the four mask bytes changed: the guards and their neighbours are intact. */
    assert(!memcmp(image+KZSF_FF_FLAGS-4,around,4));
    assert(!memcmp(image+KZSF_FF_FLAGS+4,around+8,4));
    assert(!memcmp(image+KZSF_GRAIN_GUARD,kzsf_grain_signature,sizeof(kzsf_grain_signature)));
    assert(!memcmp(image+KZSF_BLOOM_GUARD,kzsf_bloom_signature,sizeof(kzsf_bloom_signature)));

    /* Bits 4..11 survive every switch and are never used by these patches. */
    assert((mask_of(image)&0xff0u)==0xff0u);
    /* Every switch is independent: applying one leaves the others' bits alone. */
    memcpy(image,base,size);
    assert(kzsf_disable_film_grain(image,size,&before,&after) && mask_of(image)==0xff7u);
    assert(kzsf_disable_bloom(image,size,&before,&after) && mask_of(image)==0xff3u);

    /* Ordering constraint, documented in the header: the strict motion-blur guard
     * refuses once bit 3 has already been cleared, so probe.c must install the
     * motion-blur/DOF switches before these. */
    memcpy(image,base,size);
    assert(kzsf_disable_film_grain(image,size,&before,&after));
    assert(!kzsf_disable_motion_blur(image,size,&before,&after));
    assert(mask_of(image)==0xff7u);

    free(image); free(storage);
    puts("PASS: real-image guards for bloom/film grain, mask arithmetic, refusals, "
         "neighbour preservation and install ordering");
    return 0;
}
