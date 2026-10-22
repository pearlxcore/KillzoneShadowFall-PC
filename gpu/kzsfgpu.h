/* C interface of the GPU library (gpu/): shadPS4's Liverpool/Vulkan video core,
 * GnmDriver, VideoOut and kernel event queues, adapted to the native loader. */
#ifndef KZSFGPU_H
#define KZSFGPU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *title;          /* window title */
    const char *serial;         /* CUSA id, names the pipeline cache */
    const char *user_dir;       /* pipeline cache/logs directory */
    uint32_t sdk_version;       /* from the eboot's procparam */
    uint32_t psf_attributes;    /* param.sfo ATTRIBUTE */
    int32_t width, height;      /* initial window size */
} KzsfGpuConfig;
/* Registers kernel event queues (needed with or without graphics). */
void kzsfgpu_register_kernel(void);
/* Creates window, Vulkan device, presenter and GPU command processor. */
int kzsfgpu_init(const KzsfGpuConfig *config);
/* Function for an imported NID ("NID#lib#mod"), or 0 when the GPU library does not provide it. */
uintptr_t kzsfgpu_resolve(const char *scoped_nid);
/* Called first by the loader's SIGSEGV handler: 1 when a GPU page-tracking fault was handled. */
int kzsfgpu_handle_fault(void *ucontext, void *address);
/* KZSF_WRITE_LOG=1: prints the logged GPU-side writes to guest memory near the fault. */
void kzsfgpu_dump_guest_writes(void *ucontext);
void kzsfgpu_dump_write_range(uint64_t address, uint64_t size);
/* Keyboard text entry through the game window (IME dialog). begin returns 0 when
 * no window exists; poll returns 0 typing, 1 confirmed, 2 cancelled (UTF-8 text). */
int kzsfgpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8);
int kzsfgpu_text_input_poll(char *out_utf8, uint64_t size);
/* A list for the player to choose from, drawn over the game (save list). images: a PNG path per
 * item drawn beside it (NULL array or entry: none). begin: 0 when no window exists.
 * poll: -1 while open, -2 cancelled, else the chosen index. */
int kzsfgpu_choice_begin(const char *title, const char *const *items, const char *const *images, int count, int focus);
int kzsfgpu_choice_poll(void);
/* 1 while the in-game settings menu is open: the game's pad input is held neutral. */
int kzsfgpu_overlay_captures_input(void);
/* Models:0 default,1 Engineer,2 Commando,3 Security Trooper,4 Sniper,
 * 5 Special Forces,6 Infantry Trooper. Status:0 ready,1 needs gameplay,
 * 2 pending,3 unavailable,4 refused. Engine consumes requests on its own thread. */
void kzsfgpu_character_selector_enable(int enabled);
uint64_t kzsfgpu_character_request(void);
int kzsfgpu_camera_request(int camera);
void kzsfgpu_camera_publish(int camera,int active);
void kzsfgpu_character_cycle(int direction);
void kzsfgpu_character_select(int model);
void kzsfgpu_character_reset(void);
void kzsfgpu_character_publish(uint64_t token,int model,int status,uint64_t available);
/* Live controller response gain in percent; aiming is nonzero while L2 is held. */
int kzsfgpu_controller_aim_gain(int aiming);
int kzsfgpu_controller_drift_deadzone(void);
int kzsfgpu_controller_turn_speed(void);
int kzsfgpu_sprint_turn_speed(void);
int kzsfgpu_fov_percent(void);
int kzsfgpu_camera_shake(void);
int kzsfgpu_mouse_aim_enabled(void);
void kzsfgpu_free_camera_input(int active);
void kzsfgpu_free_camera_mouse(float *yaw,float *pitch);
int kzsfgpu_pc_prompts_active(void);
int kzsfgpu_look_curve(void);
int kzsfgpu_look_deadzone(void);
int kzsfgpu_look_ramp(void);
int kzsfgpu_look_assist(void);
/* Live engine vertical look-rate multiplier in percent (the pitch field, separate from heading). */
int kzsfgpu_controller_pitch_speed(void);
/* Captured mouse converted to held right-stick axes; buttons: 1 fire, 2 aim. */
int kzsfgpu_mouse_state(int16_t *x, int16_t *y, uint32_t *buttons);
/* Number of symbols registered by the vendored libraries (diagnostics). */
unsigned kzsfgpu_symbol_count(void);
#ifdef __cplusplus
}
#endif
#endif
