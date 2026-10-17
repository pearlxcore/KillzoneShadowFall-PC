/* Kernel semaphores: 32-bit IDs, counted tokens and FIFO waiters.
 * Priority-ordered semaphores are approximated by FIFO order. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
typedef struct Waiter {
    int32_t need, result;
    int done;
    unsigned long tid;
    pthread_cond_t event;
    struct Waiter *next;
} Waiter;
typedef struct Sema {
    uint32_t id, attr;
    int32_t count, initial, maximum;
    unsigned active;
    int deleted;
    Waiter *first;
    char name[32];
    struct Sema *next;
} Sema;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Sema *registry;
static uint32_t next_id=1;
static size_t created, deleted, acquired, signaled, timed_out;
static unsigned long current_tid(void) {
#ifdef _WIN32
    return (unsigned long)GetCurrentThreadId();
#else
    return (unsigned long)(uintptr_t)pthread_self();
#endif
}
/* Ring of recent semaphore operations for the watchdog dump (runtime_sema_dump):
 * the tail is the last semaphore activity before a stall. Callers hold lock. */
typedef struct { char op; uint32_t id; int32_t a, b; unsigned long tid; } SemaEvent;
static SemaEvent ring[256];
static unsigned ring_seq;
static void log_event(char op, uint32_t id, int32_t a, int32_t b) {
    ring[ring_seq&255]=(SemaEvent){op,id,a,b,current_tid()};
    ++ring_seq;
}
static Sema *find(uint32_t id) {
    for (Sema *s=registry;s;s=s->next) if (s->id==id) return s;
    return NULL;
}
static void host_check(int e) {
    if (e) { fprintf(stderr,"STOP: semaphore host error %d\n",e); exit(21); }
}
/* Waits use this clock for their deadlines. winpthreads condition variables accept only
 * CLOCK_REALTIME (pthread_condattr_setclock(CLOCK_MONOTONIC) fails with EINVAL). */
#ifdef _WIN32
#define WAIT_CLOCK CLOCK_REALTIME
#else
#define WAIT_CLOCK CLOCK_MONOTONIC
#endif
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(WAIT_CLOCK,&t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static ABI int32_t sem_create(uint32_t *out,const char *name,uint32_t attr,
                              int32_t initial,int32_t maximum,const void *options) {
    if (!out || !name || attr>2 || initial<0 || maximum<=0 || initial>maximum) return ERR(22);
    if (options) { fputs("STOP: semaphore options are not implemented\n",stderr); exit(21); }
    Sema *s=calloc(1,sizeof(*s));
    if (!s) return ERR(12);
    pthread_mutex_lock(&lock);
    if (next_id==UINT32_MAX) { pthread_mutex_unlock(&lock); free(s); return ERR(28); }
    s->id=next_id++; s->attr=attr; s->count=s->initial=initial; s->maximum=maximum;
    snprintf(s->name,sizeof(s->name),"%s",name);
    s->next=registry; registry=s; *out=s->id; ++created;
    printf("Runtime: semaphore id=%u '%s', attr=%u, initial=%d, max=%d\n",s->id,s->name,attr,initial,maximum);
    log_event('C',s->id,initial,maximum);
    pthread_mutex_unlock(&lock);
    return 0;
}
static int32_t wait_count(uint32_t id,int32_t need,uint32_t *timeout,int block) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (need<=0 || need>s->maximum) result=ERR(22);
    else if (s->count>=need) { s->count-=need; ++acquired; log_event('a',id,need,s->count); }
    else if (!block) result=ERR(16);
    else if (timeout && !*timeout) { result=ERR(60); ++timed_out; }
    else {
        /* Priority-ordered semaphores (attr 2) are served FIFO: there is no
           PS4 priority scheduler, host threads run at equal priority. */
        if (getenv("GOW3_TRACE_SEMA")) fprintf(stderr,"Runtime: blocking wait on semaphore %u '%s' (need %d, count %d)\n",id,s->name,need,s->count); /* deadlock diagnosis */
        log_event('w',id,need,s->count);
        Waiter w={.need=need,.tid=current_tid()};
        pthread_condattr_t attr;
        host_check(pthread_condattr_init(&attr));
        host_check(pthread_condattr_setclock(&attr,WAIT_CLOCK));
        host_check(pthread_cond_init(&w.event,&attr));
        host_check(pthread_condattr_destroy(&attr));
        Waiter **tail=&s->first;
        while (*tail) tail=&(*tail)->next;
        *tail=&w; ++s->active;
        uint64_t deadline=timeout ? now_ns()+(uint64_t)*timeout*1000 : 0;
        struct timespec end={.tv_sec=(time_t)(deadline/1000000000),.tv_nsec=(long)(deadline%1000000000)};
        while (!w.done) {
            int e=timeout ? pthread_cond_timedwait(&w.event,&lock,&end) : pthread_cond_wait(&w.event,&lock);
            if (e==ETIMEDOUT && !w.done) {
                Waiter **p=&s->first;
                while (*p!=&w) p=&(*p)->next;
                *p=w.next; w.result=ERR(60); w.done=1; ++timed_out;
            } else if (e!=ETIMEDOUT) host_check(e);
        }
        result=w.result;
        if (!result) ++acquired;
        log_event('x',id,result,s->count);
        if (timeout) {
            uint64_t now=now_ns();
            *timeout=result ? 0 : (uint32_t)(now>=deadline ? 0 : (deadline-now)/1000);
        }
        host_check(pthread_cond_destroy(&w.event));
        if (!--s->active && s->deleted) free(s);
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t sem_wait(uint32_t id,int32_t need,uint32_t *timeout) { return wait_count(id,need,timeout,1); }
static ABI int32_t sem_poll(uint32_t id,int32_t need) { return wait_count(id,need,NULL,0); }
static ABI int32_t sem_signal(uint32_t id,int32_t count) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (count<=0 || count>s->maximum-s->count) result=ERR(22);
    else {
        s->count+=count; ++signaled;
        log_event('s',id,count,s->count);
        Waiter **p=&s->first;
        while (*p) {
            Waiter *w=*p;
            if (w->need>s->count) { p=&w->next; continue; }
            s->count-=w->need; *p=w->next; w->done=1;
            host_check(pthread_cond_signal(&w->event));
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static int32_t wake_all(Sema *s,int32_t result) {
    int32_t count=0;
    while (s->first) {
        Waiter *w=s->first; s->first=w->next;
        w->result=result; w->done=1; ++count;
        host_check(pthread_cond_signal(&w->event));
    }
    return count;
}
static ABI int32_t sem_cancel(uint32_t id,int32_t count,int32_t *waiters) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (count>s->maximum) result=ERR(22);
    else {
        int32_t n=wake_all(s,ERR(85));
        if (waiters) *waiters=n;
        s->count=count<0 ? s->initial : count;
        log_event('k',id,count,n);
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t sem_delete(uint32_t id) {
    pthread_mutex_lock(&lock);
    Sema **p=&registry;
    while (*p && (*p)->id!=id) p=&(*p)->next;
    if (!*p) { pthread_mutex_unlock(&lock); return ERR(3); }
    Sema *s=*p; *p=s->next; s->deleted=1; ++deleted;
    log_event('d',id,s->count,0);
    wake_all(s,ERR(13));
    if (!s->active) free(s);
    pthread_mutex_unlock(&lock);
    return 0;
}
/* Read-only test/diagnostic snapshot, not exposed to guest code. */
unsigned runtime_sema_waiters(uint32_t id) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id); unsigned n=0;
    if (s) for (Waiter *w=s->first;w;w=w->next) ++n;
    pthread_mutex_unlock(&lock);
    return n;
}
/* POSIX semaphores (libScePosix): a kernel semaphore id stored in the guest sem_t.
 * POSIX calls return 0 or -1 and set errno; the guest's wait loops test errno
 * (EINTR=4 retries, ETIMEDOUT=60 finishes), so the SCE error must become an errno. */
static int32_t posix_errno(int32_t result) {
    int e=(int)((uint32_t)result & 0xff);
    if (result==ERR(60)) e=60;      /* ETIMEDOUT */
    else if (result==ERR(3)) e=22;  /* EINVAL: no such semaphore */
    else if (!e) e=22;
    *runtime_errno()=(int32_t)e;
    return -1;
}
static ABI int32_t posix_sem_init(uint32_t *sem, int pshared, uint32_t value) {
    (void)pshared;
    if (!sem) return posix_errno(ERR(22));
    uint32_t id=0;
    int32_t r=sem_create(&id,"posix",0,(int32_t)value,0x7fffffff,NULL);
    if (r) return posix_errno(r);
    *sem=id; return 0;
}
static ABI int32_t posix_sem_destroy(uint32_t *sem) {
    if (!sem) return posix_errno(ERR(22));
    int32_t r=sem_delete(*sem);
    return r ? posix_errno(r) : 0;
}
static ABI int32_t posix_sem_wait(uint32_t *sem) {
    if (!sem) return posix_errno(ERR(22));
    int32_t r=sem_wait(*sem,1,NULL);
    return r ? posix_errno(r) : 0;
}
static ABI int32_t posix_sem_post(uint32_t *sem) {
    if (!sem) return posix_errno(ERR(22));
    int32_t r=sem_signal(*sem,1);
    return r ? posix_errno(r) : 0;
}
static ABI int32_t posix_sem_timedwait(uint32_t *sem, const struct timespec *abstime) {
    if (!sem || !abstime) return posix_errno(ERR(22));
    struct timespec now;
    if (clock_gettime(WAIT_CLOCK,&now)) return posix_errno(ERR(22));
    int64_t us=((int64_t)abstime->tv_sec-now.tv_sec)*1000000+((int64_t)abstime->tv_nsec-now.tv_nsec)/1000;
    uint32_t timeout=us>0 ? (uint32_t)us : 0;
    int32_t r=sem_wait(*sem,1,&timeout);
    return r ? posix_errno(r) : 0;
}
uintptr_t runtime_sema_resolve(const char *name) {
    if (!strcmp(name,"pDuPEf3m4fI#libkernel")) return (uintptr_t)posix_sem_init;
    if (!strcmp(name,"cDW233RAwWo#libkernel")) return (uintptr_t)posix_sem_destroy;
    if (!strcmp(name,"w5IHyvahg-o#libkernel")) return (uintptr_t)posix_sem_timedwait;
    if (!strcmp(name,"YCV5dGGBcCo#libkernel")) return (uintptr_t)posix_sem_wait;
    if (!strcmp(name,"IKP8typ0QUk#libkernel")) return (uintptr_t)posix_sem_post;
    if (!strcmp(name,"188x57JYp0g#libkernel")) return (uintptr_t)sem_create;
    if (!strcmp(name,"Zxa0VhQVTsk#libkernel")) return (uintptr_t)sem_wait;
    if (!strcmp(name,"4czppHBiriw#libkernel")) return (uintptr_t)sem_signal;
    if (!strcmp(name,"12wOHk8ywb0#libkernel")) return (uintptr_t)sem_poll;
    if (!strcmp(name,"4DM06U2BNEY#libkernel")) return (uintptr_t)sem_cancel;
    if (!strcmp(name,"R1Jvn8bSCW8#libkernel")) return (uintptr_t)sem_delete;
    return 0;
}
void runtime_sema_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: semaphores created=%zu, deleted=%zu, acquired=%zu, signals=%zu, timeouts=%zu\n",
           created,deleted,acquired,signaled,timed_out);
    pthread_mutex_unlock(&lock);
}
/* Watchdog dump: every live semaphore with its blocked waiters, then the most recent
 * semaphore events (the tail is the last semaphore activity before a stall). Called from
 * the loader's freeze/stall watchdogs after dump_thread_rips resumed every thread. */
void runtime_sema_dump(void) {
    pthread_mutex_lock(&lock);
    fprintf(stderr,"  semaphores live:\n");
    for (Sema *s=registry;s;s=s->next) {
        fprintf(stderr,"    id=%u '%s' count=%d max=%d waiters=%u",s->id,s->name,s->count,s->maximum,s->active);
        for (Waiter *w=s->first;w;w=w->next)
            fprintf(stderr," [tid=%lu need=%d]",w->tid,(int)w->need);
        fputc('\n',stderr);
    }
    if (ring_seq) {
        fprintf(stderr,"    recent semaphore events (oldest first):\n");
        unsigned total=ring_seq, first=total>256 ? total-256 : 0, shown=0;
        for (unsigned i=first;i<total && shown<48;++i) {
            SemaEvent e=ring[i&255];
            const char *name="?";
            for (Sema *s=registry;s;s=s->next) if (s->id==e.id) { name=s->name; break; }
            fprintf(stderr,"      %c id=%u '%s' a=%d b=%d tid=%lu\n",e.op,e.id,name,(int)e.a,(int)e.b,e.tid);
            ++shown;
        }
    }
    pthread_mutex_unlock(&lock);
}

