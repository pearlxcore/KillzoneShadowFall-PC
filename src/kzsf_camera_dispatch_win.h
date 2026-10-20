#ifndef KZSF_CAMERA_DISPATCH_WIN_H
#define KZSF_CAMERA_DISPATCH_WIN_H
#include <windows.h>
typedef void (*KzsfCameraTask)(void *);
/* The camera's mandatory per-frame update must not depend on a best-effort
 * observer snapshot lock. This is called outside VEH. Observer VEH handlers
 * never acquire the follow lock. */
static inline void kzsf_camera_dispatch(SRWLOCK *follow_lock,SRWLOCK *observer_lock,
        KzsfCameraTask follow,KzsfCameraTask observe,void *argument) {
    if(follow) {
        AcquireSRWLockExclusive(follow_lock);
        follow(argument);
        ReleaseSRWLockExclusive(follow_lock);
    }
    if(observe && TryAcquireSRWLockExclusive(observer_lock)) {
        observe(argument);
        ReleaseSRWLockExclusive(observer_lock);
    }
}
#endif
