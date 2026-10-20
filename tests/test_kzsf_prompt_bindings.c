/* Uses the actual pad parser and resulting map, including invalid-remap fallback. */
#define _GNU_SOURCE
#include <assert.h>
#include "../src/runtime_pad.c"
int gow3gpu_overlay_captures_input(void) {return 0;}
int gow3gpu_controller_drift_deadzone(void) {return 8;}
int gow3gpu_controller_aim_gain(int aiming) {(void)aiming;return 100;}
int gow3gpu_mouse_state(int16_t *x,int16_t *y,uint32_t *buttons) {(void)x;(void)y;(void)buttons;return 0;}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    (void)table;(void)count;(void)name;return 0;
}
int main(void) {
    _putenv_s("GOW3_KEY_MAP","cross=Q,square=Left Shift,r2=NotAKey,triangle=Application");
    _putenv_s("GOW3_PAD_MAP","");
    char label[16];
    assert(runtime_pad_prompt_binding(2,0,label,sizeof(label))==1 && !strcmp(label,"Q"));
    assert(runtime_pad_prompt_binding(3,0,label,sizeof(label))==1 && !strcmp(label,"SHIFT"));
    assert(runtime_pad_prompt_binding(5,0,label,sizeof(label))==1 && !strcmp(label,"F"));
    assert(runtime_pad_prompt_binding(5,1,label,sizeof(label))==2 && !*label);
    assert(runtime_pad_prompt_binding(4,1,label,sizeof(label))==3 && !*label);
    assert(runtime_pad_prompt_binding(4,0,label,sizeof(label))==1 && !strcmp(label,"R"));
    assert(!runtime_pad_prompt_binding(0,0,label,sizeof(label))); /* too-long label -> original */
    assert(!runtime_pad_prompt_binding(9,0,label,sizeof(label))); /* ambiguous touchpad */
    assert(!runtime_pad_prompt_binding(10,0,label,sizeof(label))); /* stick gesture */
    assert(!runtime_pad_prompt_binding(11,0,label,sizeof(label)));
    assert(!runtime_pad_prompt_binding(16,0,label,sizeof(label))); /* Move */
    assert(!runtime_pad_prompt_binding(2,0,label,1));
    assert(!runtime_pad_prompt_binding(2,0,NULL,16));
    assert(apply_button_map(&key_map,"r2=Backspace",true)==1);
    assert(runtime_pad_prompt_binding(5,0,label,sizeof(label))==1 && !strcmp(label,"BKSP"));
    assert(apply_button_map(&key_map,"cross=Space",true)==1);
    assert(runtime_pad_prompt_binding(2,0,label,sizeof(label))==1 && !strcmp(label,"SPC"));
    assert(runtime_pad_prompt_key(SDL_SCANCODE_SPACE));
    assert(runtime_pad_prompt_key(SDL_SCANCODE_W));
    assert(!runtime_pad_prompt_key(SDL_SCANCODE_F24));
    assert(runtime_pad_prompt_button(SDL_GAMEPAD_BUTTON_SOUTH));
    assert(!runtime_pad_prompt_button(-1));
    injected.buttons=BTN_CROSS;
    runtime_pad_free_camera(1);
    PadData d;sample(&d);
    assert(!d.buttons && d.left_x==128 && d.right_y==128 && !d.l2 && !d.r2);
    runtime_pad_free_camera(0);
    float lx=1,ly=1,rx=1,ry=1,forward=1;
    runtime_pad_free_camera_axes(&lx,&ly,&rx,&ry,&forward);
    assert(!lx && !ly && !rx && !ry && !forward); // inactive camera does not sample/consume the gamepad
    injected.buttons=0;
    puts("PC prompt bindings: parsed remaps, mouse modes, invalid map and gesture fallbacks pass.");
}
