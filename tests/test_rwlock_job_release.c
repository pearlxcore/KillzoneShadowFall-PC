/* Shared-lock lifetime crosses job threads in Killzone's task destructors. */
#include "../src/runtime_rwlock.c"
#include <assert.h>
#include <sched.h>
#include <stdatomic.h>

static Rwlock *job_lock;
static int32_t release_result;
static atomic_int cleanup_acquired;
static void *release_job(void *unused) {
    (void)unused;
    release_result=rw_unlock(&job_lock);
    return NULL;
}
static void finish_job(void) {
    pthread_t thread;
    assert(pthread_create(&thread,NULL,release_job,NULL)==0);
    assert(pthread_join(thread,NULL)==0);
}
static void *cleanup_job(void *unused) {
    (void)unused;
    assert(rw_write(&job_lock)==0);
    atomic_store(&cleanup_acquired,1);
    assert(rw_unlock(&job_lock)==0);
    return NULL;
}
int main(void) {
    assert(rw_read(&job_lock)==0 && rw_read(&job_lock)==0);
    pthread_t cleanup;
    assert(pthread_create(&cleanup,NULL,cleanup_job,NULL)==0);
    int pending=0;
    for (unsigned i=0; i<100000 && !pending; ++i) {
        pthread_mutex_lock(&registry_lock);
        pending=job_lock->inflight!=0;
        pthread_mutex_unlock(&registry_lock);
        if (!pending) sched_yield();
    }
    assert(pending);
    finish_job();
    if (release_result) {
        fprintf(stderr,"FAIL: worker shared unlock returned 0x%08x\n",(unsigned)release_result);
        return 1;
    }
    unsigned readers=0;
    pthread_mutex_lock(&registry_lock);
    for (Holder *h=job_lock->holders; h; h=h->next) readers+=h->readers;
    pthread_mutex_unlock(&registry_lock);
    assert(readers==1);
    assert(!atomic_load(&cleanup_acquired));
    finish_job(); assert(release_result==0);
    assert(pthread_join(cleanup,NULL)==0);
    assert(atomic_load(&cleanup_acquired) && job_lock->holders==NULL);
    assert(rw_write(&job_lock)==0);
    finish_job(); assert((uint32_t)release_result==UINT32_C(0x80020001));
    assert(rw_unlock(&job_lock)==0);
    finish_job(); assert((uint32_t)release_result==UINT32_C(0x80020001));
    assert(rw_destroy(&job_lock)==0);
    puts("PASS: worker shared unlock, drained cleanup, exclusive owner checks");
    return 0;
}
