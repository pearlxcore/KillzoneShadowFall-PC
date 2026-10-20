#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_third_person.h"
int main(void) {
    KzsfThirdPerson s={0};KzsfFreeCamera native={0};
    native.matrix[15]=1;native.heading=0;native.pitch=0;kzsf_free_camera_basis(&native);
    native.matrix[12]=10;native.matrix[13]=20;native.matrix[14]=30;
    float out[16],saved[16];memset(out,0xa5,sizeof(out));memcpy(saved,out,sizeof(out));
    /* Execution alternatives preserve the source orientation, follow roll,
     * and measure the swept distance rather than the desired distance. */
    for(unsigned index=0;index<3;index++) {
        assert(kzsf_third_person_combat_candidate(native.matrix,index,out));
        assert(!memcmp(out,native.matrix,12*sizeof(float)));
        float length=kzsf_third_person_clearance(native.matrix,out,1);
        assert(length>1.7f && length<2.1f);
        assert(kzsf_third_person_clearance(native.matrix,out,0)==0);
        assert(fabsf(kzsf_third_person_clearance(native.matrix,out,.25f)-length*.25f)<1e-6f);
        assert(kzsf_third_person_converge(native.matrix,out,25));
    }
    memcpy(out,saved,sizeof(out));
    assert(!kzsf_third_person_combat_candidate(native.matrix,3,out));assert(!memcmp(out,saved,sizeof(out)));
    assert(kzsf_third_person_clearance(native.matrix,out,-.1f)<0);
    assert(kzsf_third_person_clearance(native.matrix,out,NAN)<0);
    assert(!kzsf_third_person_step(&s,1,100,1,0,1,native.matrix,out));assert(!memcmp(out,saved,sizeof(out)));
    assert(kzsf_third_person_step(&s,1,116,1,1,1,native.matrix,out));
    assert(fabsf(out[12]-8.2f)<1e-5f && fabsf(out[13]-19.65f)<1e-5f && fabsf(out[14]-30.12f)<1e-5f);
    assert(!memcmp(out,native.matrix,12*sizeof(float)));
    assert(kzsf_third_person_step(&s,1,132,1,1,1,native.matrix,out));assert(s.requested);
    memcpy(saved,out,sizeof(out));
    assert(!kzsf_third_person_step(&s,1,148,1,0,0,native.matrix,out));assert(s.requested);
    assert(!memcmp(out,saved,sizeof(out))); /* Native ADS/state fallback leaves output untouched. */
    assert(kzsf_third_person_step(&s,1,164,1,0,1,native.matrix,out));
    assert(!kzsf_third_person_step(&s,1,180,0,0,1,native.matrix,out));
    assert(kzsf_third_person_step(&s,1,196,1,0,1,native.matrix,out));
    assert(!kzsf_third_person_step(&s,1,212,1,1,1,native.matrix,out));assert(!s.requested);
    kzsf_third_person_step(&s,1,228,1,0,1,native.matrix,out);
    assert(kzsf_third_person_step(&s,1,244,1,1,1,native.matrix,out));
    assert(!kzsf_third_person_step(&s,2,260,1,0,1,native.matrix,out));assert(!s.requested);
    s.requested=1;assert(kzsf_third_person_step(&s,2,1000,1,0,1,native.matrix,out));assert(s.requested);
    /* Repeated selector audits/streaming hitches must not undo F8. Unknown
     * player states and focus loss still fall back without forgetting intent. */
    KzsfThirdPerson hitches={.requested=1};
    const uint64_t times[]={100,351,1300,6400,12000};
    for(unsigned i=0;i<sizeof(times)/sizeof(*times);i++) {
        assert(kzsf_third_person_step(&hitches,5,times[i],1,0,1,native.matrix,out));
        assert(hitches.requested);
    }
    assert(!kzsf_third_person_step(&hitches,5,18000,1,0,0,native.matrix,out) && hitches.requested);
    assert(!kzsf_third_person_step(&hitches,5,24000,0,0,1,native.matrix,out) && hitches.requested);
    assert(kzsf_third_person_step(&hitches,5,30000,1,0,1,native.matrix,out));
    assert(!kzsf_third_person_step(&hitches,5,30016,1,1,1,native.matrix,out) && !hitches.requested);
    hitches.requested=1;
    assert(!kzsf_third_person_step(&hitches,5,100,1,0,1,native.matrix,out) && !hitches.requested);
    s.requested=1;native.matrix[0]=NAN;assert(!kzsf_third_person_step(&s,2,1016,1,0,1,native.matrix,out));
    /* Camera follows each new native pose; it never accumulates the previous offset. */
    native.heading=1;native.pitch=.4f;kzsf_free_camera_basis(&native);
    for(unsigned i=0;i<1000;i++){native.matrix[12]+=.01f;
        assert(kzsf_third_person_step(&s,2,1032+i*16,1,0,1,native.matrix,out));
        float distance=0;for(int j=0;j<3;j++){float d=out[12+j]-native.matrix[12+j];distance+=d*d;}
        assert(fabsf(distance-(1.8f*1.8f+.35f*.35f+.12f*.12f))<.001f);}
    /* Different target ranges and shortened arms must project the native aim
     * point at the display camera's center, including pitched/turned views. */
    const float ranges[]={0,.25f,2,10,100,250};
    for(unsigned r=0;r<sizeof(ranges)/sizeof(*ranges);r++)for(unsigned boom=1;boom<=4;boom++) {
        memcpy(out,native.matrix,sizeof(out));
        for(int i=0;i<3;i++)out[12+i]+=((float)boom/4)*(-1.8f*out[4+i]+.35f*out[i]+.12f*out[8+i]);
        float eye[3]={out[12],out[13],out[14]};
        assert(kzsf_third_person_converge(native.matrix,out,ranges[r]));
        float horizontal=0,vertical=0,forward=0;
        for(int i=0;i<3;i++) {
            float d=native.matrix[12+i]+ranges[r]*native.matrix[4+i]-out[12+i];
            horizontal+=d*out[i];vertical+=d*out[8+i];forward+=d*out[4+i];assert(out[12+i]==eye[i]);
        }
        assert(fabsf(horizontal)<.0001f && fabsf(vertical)<.0001f && forward>0);
    }
    memcpy(saved,out,sizeof(out));assert(!kzsf_third_person_converge(native.matrix,out,NAN));
    assert(!memcmp(saved,out,sizeof(out)));
    memcpy(out,native.matrix,sizeof(out));out[12]+=.4f;
    assert(kzsf_third_person_body_hidden(0,native.matrix,out));
    out[12]=native.matrix[12]+.6f;
    assert(kzsf_third_person_body_hidden(1,native.matrix,out));
    assert(!kzsf_third_person_body_hidden(0,native.matrix,out));
    out[12]=native.matrix[12]+.7f;
    assert(!kzsf_third_person_body_hidden(1,native.matrix,out));
    memcpy(out,native.matrix,sizeof(out));memcpy(saved,out,sizeof(out));
    assert(kzsf_third_person_converge(native.matrix,out,0));
    assert(!memcmp(saved,out,sizeof(out))); /* Fully collapsed camera at zero-distance hit. */
    /* Zero gravity may roll through a full turn. Boom and aim convergence
     * follow native right/forward/up, rather than snapping to world Z. */
    float reference[16];memcpy(reference,native.matrix,sizeof(reference));
    KzsfThirdPerson space={.requested=1};
    for(int angle=0;angle<=360;angle+=15) {
        float roll=angle*0.01745329252f,c=cosf(roll),sn=sinf(roll);
        for(int i=0;i<3;i++) {
            native.matrix[i]=c*reference[i]+sn*reference[8+i];
            native.matrix[8+i]=-sn*reference[i]+c*reference[8+i];
        }
        assert(kzsf_third_person_step(&space,3,100+angle,1,0,1,native.matrix,out));
        for(int i=0;i<3;i++)assert(fabsf(out[12+i]-native.matrix[12+i]+1.8f*native.matrix[4+i]-.35f*native.matrix[i]-.12f*native.matrix[8+i])<.0001f);
        assert(!memcmp(out,native.matrix,12*sizeof(float)));
        assert(kzsf_third_person_converge(native.matrix,out,25));
        float up_dot=0;for(int i=0;i<3;i++)up_dot+=out[8+i]*native.matrix[8+i];
        assert(up_dot>.99f && kzsf_free_camera_valid(out));
        for(unsigned index=0;index<3;index++) {
            assert(kzsf_third_person_combat_candidate(native.matrix,index,out));
            float length=kzsf_third_person_clearance(native.matrix,out,1);
            assert(length>1.7f && length<2.1f);
            assert(!memcmp(out,native.matrix,12*sizeof(float)));
        }
    }
    puts("Third-person: slow-frame intent persistence, native roll, toggle/fallback, aim and body hiding pass.");
}
