#include "../gpu/shim/kzsf_prompt_device.h"
#include <cassert>
#include <cstdio>
int main() {
    KillzonePromptDevice d;
    assert(!d.Stick(2,1000,8)); // drift below the existing radial guard
    assert(!d.Stick(3,-1000,8));
    assert(d.Stick(2,20000,8));
    assert(!d.Stick(2,21000,8)); // held stick cannot steal keyboard selection
    assert(!d.Stick(3,12000,8));
    assert(!d.Stick(2,0,8));
    assert(!d.Stick(3,0,8));
    assert(d.Stick(3,-20000,8)); // fresh action after neutral
    assert(!d.Trigger(0,100));
    assert(d.Trigger(0,12000));
    assert(!d.Trigger(0,13000));
    assert(!d.Trigger(0,0));
    assert(d.Trigger(0,20000));
    assert(!d.Stick(4,30000,8));
    assert(!d.Trigger(2,30000));
    puts("Prompt device: drift, held sticks, recentering and trigger edges pass.");
}
