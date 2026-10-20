#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_camera_dispatch_win.h"
static SRWLOCK follow_lock=SRWLOCK_INIT,observer_lock=SRWLOCK_INIT;
static unsigned follows,observations;static HANDLE done;
static void follow(void *opaque){assert(opaque==(void*)42);++follows;}
static void observe(void *opaque){assert(opaque==(void*)42);++observations;}
static DWORD WINAPI worker(void *opaque) {
    kzsf_camera_dispatch(&follow_lock,&observer_lock,follow,observe,opaque);SetEvent(done);return 0;
}
int main(void) {
    /* Regression: diagnostic contention previously skipped the pose override
     * silently, while leaving the body-active flag set from the previous frame. */
    AcquireSRWLockExclusive(&observer_lock);
    for(unsigned i=0;i<100;i++)kzsf_camera_dispatch(&follow_lock,&observer_lock,follow,observe,(void*)42);
    assert(follows==100 && observations==0);
    ReleaseSRWLockExclusive(&observer_lock);
    kzsf_camera_dispatch(&follow_lock,&observer_lock,follow,observe,(void*)42);
    assert(follows==101 && observations==1);
    /* Follow work queues rather than disappearing if another view is updating. */
    done=CreateEventW(NULL,TRUE,FALSE,NULL);assert(done);
    AcquireSRWLockExclusive(&follow_lock);
    HANDLE thread=CreateThread(NULL,0,worker,(void*)42,0,NULL);assert(thread);
    assert(WaitForSingleObject(done,20)==WAIT_TIMEOUT);
    ReleaseSRWLockExclusive(&follow_lock);
    assert(WaitForSingleObject(done,2000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    assert(follows==102 && observations==2);CloseHandle(thread);CloseHandle(done);
    kzsf_camera_dispatch(&follow_lock,&observer_lock,NULL,observe,(void*)42);
    assert(follows==102 && observations==3);
    puts("Camera dispatch: 100 mandatory follow frames survive observer contention; follow contention queues; observer-only mode passes.");
}
