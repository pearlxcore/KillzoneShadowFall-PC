#ifndef KZSF_CONTROLLER_AIM_H
#define KZSF_CONTROLLER_AIM_H
#include <stdint.h>
#include <math.h>

/* Response gain, not an engine turn-rate patch. Preserve direction when saturating,
 * exact neutral and the original SDL values at 100%. No temporal smoothing. */
static inline void kzsf_controller_aim(int16_t *x, int16_t *y, int percent) {
    if (percent < 50 || percent > 300 || percent == 100) return;
    float sx=(float)*x*(float)percent/100.0f;
    float sy=(float)*y*(float)percent/100.0f;
    float scale=fmaxf(1.0f,fmaxf(sx/(sx<0 ? -32768.0f : 32767.0f),
                                sy/(sy<0 ? -32768.0f : 32767.0f)));
    *x=(int16_t)lroundf(sx/scale);
    *y=(int16_t)lroundf(sy/scale);
}
#endif
