#ifndef KZSF_CONTROLLER_AIM_H
#define KZSF_CONTROLLER_AIM_H
#include <stdint.h>
#include <math.h>

/* Physical right-stick neutral guard, BEFORE sensitivity gain. Preserve every
 * value outside the radial guard so this does not add another response curve.
 * Mouse input is merged later and never passes through this filter. */
static inline int kzsf_controller_drift_guard(int16_t *x,int16_t *y,int percent) {
    if(percent<=0 || percent>30) return 0;
    float fx=(float)*x,fy=(float)*y,limit=32768.0f*(float)percent/100.0f;
    if(fx*fx+fy*fy<=limit*limit) {
        int suppressed=*x!=0 || *y!=0;*x=*y=0;return suppressed;
    }
    return 0;
}

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
