#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_character_generic_pose.h"
static void identity(float *m){memset(m,0,64);for(int i=0;i<4;i++)m[i*5]=1;}
int main(void) {
    assert(kzsf_character_finger_name("thumbOne_left"));
    assert(kzsf_character_finger_name("pinkyThree_right"));
    assert(!kzsf_character_finger_name("indexTwo_left_Helper"));
    assert(!kzsf_character_finger_name("middleZero_extra_left"));
    assert(!kzsf_character_finger_name("wrist_left"));
    KzsfCharacterGenericPlan plan={.from_count=78,.to_count=78};
    float input[78][16],before[78][16],output[78][16];
    for(int i=0;i<78;i++) {
        identity(input[i]);identity(plan.correction[i]);identity(plan.local[i]);
        plan.source[i]=(int16_t)i;plan.parent[i]=i ? (int16_t)(i-1) : -1;
    }
    plan.finger[19]=plan.finger[20]=1;
    plan.local[19][12]=.04f;plan.local[20][12]=.03f;
    input[18][0]=0;input[18][1]=1;input[18][4]=-1;input[18][5]=0;
    input[18][12]=2;input[18][13]=3;input[18][14]=4;
    const float c=.70710678f;
    input[19][0]=c;input[19][1]=c;input[19][4]=-c;input[19][5]=c;
    input[19][12]=10;input[20][12]=12; // detached points from independent conversion
    assert(kzsf_character_generic_worlds(&plan,input,before));
    assert(kzsf_character_generic_draw_worlds(&plan,input,output));
    assert(fabsf(output[19][12]-2)<1e-6f && fabsf(output[19][13]-3.04f)<1e-6f && output[19][14]==4);
    assert(fabsf(output[20][12]-(2+.03f*c))<1e-6f && fabsf(output[20][13]-(3.04f+.03f*c))<1e-6f);
    for(int i=0;i<78;i++) {
        assert(!memcmp(output[i],before[i],12*4)); // rotation/basis unchanged
        if(!plan.finger[i])assert(!memcmp(output[i],before[i],64));
    }
    plan.parent[19]=19;
    assert(!kzsf_character_generic_draw_worlds(&plan,input,output));
    plan.parent[19]=18;
    plan.wrist[18]=1;plan.parent[18]=17;plan.local[18][12]=.25f;
    input[17][12]=1;input[17][13]=2;input[17][14]=3;
    assert(kzsf_character_generic_draw_worlds(&plan,input,output));
    assert(fabsf(output[18][12]-1.25f)<1e-6f && output[18][13]==2 && output[18][14]==3);
    assert(!memcmp(output[18],input[18],12*4));
    assert(fabsf(output[19][12]-1.25f)<1e-6f && fabsf(output[19][13]-2.04f)<1e-6f);
    puts("hand pose: nested finger lengths follow rotated wrist/parent, rotations and unrelated joints unchanged");
}
