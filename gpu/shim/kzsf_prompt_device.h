#pragma once
#include <stdint.h>
#include "../../src/kzsf_controller_aim.h"

// Event-thread state: select a controller only on a fresh press or stick leaving
// neutral. A held stick cannot repeatedly steal prompts from keyboard/mouse.
struct KillzonePromptDevice {
    int16_t axes[4]{};
    bool active[4]{};
    bool Stick(unsigned index, int16_t value, int deadzone) {
        if(index>=4)return false;
        axes[index]=value;
        unsigned pair=index/2;
        int16_t x=axes[pair*2],y=axes[pair*2+1];
        kzsf_controller_drift_guard(&x,&y,deadzone);
        bool now=x || y, changed=now && !active[pair];
        active[pair]=now;return changed;
    }
    bool Trigger(unsigned index,int16_t value) {
        if(index>=2)return false;
        bool now=value>30*32767/255,changed=now && !active[2+index];
        active[2+index]=now;return changed;
    }
};
