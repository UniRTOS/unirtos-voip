#include "qcm_voip_port.h"
#include <errno.h>

struct voip_timer { qosa_timer_t timer; void (*cb)(void *); void *arg; };
static void timer_callback(void *arg)
{
    struct voip_timer *timer=arg;
    if (timer->cb) timer->cb(timer->arg);
}
int quec_rtos_timer_create(quec_timer_t *out)
{
    struct voip_timer *timer;
    if (!out) return -1;
    *out=NULL;
    timer=calloc(1,sizeof(*timer));
    if (!timer) return -1;
    if (qosa_timer_create(&timer->timer,timer_callback,timer)) { free(timer); return -1; }
    *out=timer;
    return 0;
}
int voip_timer_start(quec_timer_t timer,uint32_t ms,bool repeat,void (*cb)(void *),void *arg)
{
    if (!timer || !cb || !ms) return -1;
    qosa_timer_stop(timer->timer);
    timer->cb=cb;
    timer->arg=arg;
    return qosa_timer_start(timer->timer,ms,repeat);
}
int quec_rtos_timer_stop(quec_timer_t timer) { return timer ? qosa_timer_stop(timer->timer) : -1; }
bool voip_timer_running(quec_timer_t timer) { return timer && qosa_timer_is_running(timer->timer); }
int quec_rtos_timer_delete(quec_timer_t timer)
{
    int ret;
    if (!timer) return -1;
    qosa_timer_stop(timer->timer);
    ret=qosa_timer_delete(timer->timer);
    if (!ret) free(timer);
    return ret;
}
uint64_t voip_monotonic_ms(void)
{
    static uint32_t previous;
    static uint64_t elapsed;
    uint64_t value;
    uint32_t now;
    qosa_enter_critical();
    now=qosa_get_running_time();
    elapsed+=(uint32_t)(now-previous);
    previous=now;
    value=elapsed;
    qosa_exit_critical();
    return value;
}
int voip_gettimeofday(struct timeval *tv,void *tz)
{
    uint64_t ms=voip_monotonic_ms();
    (void)tz;
    if (!tv) return -1;
    tv->tv_sec=ms/1000;
    tv->tv_usec=(ms%1000)*1000;
    return 0;
}
int voip_clock_gettime(int clock_id,struct timespec *ts)
{
    uint64_t ms;
    if (!ts) return -1;
    ms=(clock_id==CLOCK_REALTIME) ? (uint64_t)qosa_get_system_time_milliseconds() : voip_monotonic_ms();
    ts->tv_sec=ms/1000;
    ts->tv_nsec=(ms%1000)*1000000;
    return 0;
}
int voip_nanosleep(const struct timespec *req,struct timespec *rem)
{
    uint64_t ms;
    if (!req || req->tv_sec<0 || req->tv_nsec<0 || req->tv_nsec>=1000000000L) return EINVAL;
    ms=(uint64_t)req->tv_sec*1000+(req->tv_nsec+999999)/1000000;
    while (ms) { uint32_t part=ms>60000 ? 60000 : (uint32_t)ms; qosa_task_sleep_ms(part); ms-=part; }
    if (rem) memset(rem,0,sizeof(*rem));
    return 0;
}
void voip_log_printf(const char *fmt,...)
{
    char text[192];
    va_list args;
    va_start(args,fmt);
    vsnprintf(text,sizeof(text),fmt,args);
    va_end(args);
    qosa_log_printf(QOSA_LOG_LEVEL_INFO,LOG_TAG_CALL,__func__,__LINE__,"VOIP %s",text);
}
struct voip_thread { qosa_task_t task; qosa_sem_t done; void *(*fn)(void *); void *arg; void *result; };
static void thread_main(void *arg)
{
    struct voip_thread *thread=arg;
    qosa_task_t self;
    qosa_sem_t done=thread->done;
    qosa_task_get_current_ref(&self);
    thread->result=thread->fn(thread->arg);
    qosa_sem_release(done);
    /* The joining task may free thread after the signal. */
    qosa_task_delete(self);
}
int voip_thread_create(voip_thread_t *out,const voip_thread_attr_t *attr,void *(*fn)(void *),void *arg)
{
    struct voip_thread *thread;
    uint32_t stack=attr && attr->stackSize ? attr->stackSize : 8192;
    if (!out || !fn) return EINVAL;
    *out=NULL;
    thread=calloc(1,sizeof(*thread));
    if (!thread) return ENOMEM;
    thread->fn=fn; thread->arg=arg;
    if (qosa_sem_create(&thread->done,0)) { free(thread); return ENOMEM; }
    if (qosa_task_create(&thread->task,stack,QOSA_PRIORITY_NORMAL,attr && attr->taskName ? attr->taskName : "voip",thread_main,thread)) {
        qosa_sem_delete(thread->done); free(thread); return ENOMEM;
    }
    *out=thread;
    return 0;
}
int voip_thread_join(voip_thread_t thread,void **result)
{
    if (!thread || (unsigned long)thread->task==voip_thread_self()) return EINVAL;
    if (qosa_sem_wait(thread->done,QOSA_WAIT_FOREVER)) return EIO;
    if (result) *result=thread->result;
    qosa_sem_delete(thread->done);
    free(thread);
    return 0;
}
unsigned long voip_thread_self(void) { qosa_task_t self=NULL; qosa_task_get_current_ref(&self); return (unsigned long)self; }
struct cond_waiter { struct cond_waiter *next; qosa_sem_t sem; bool signaled; };
struct voip_cond { qosa_mutex_t lock; struct cond_waiter *waiters; };
int voip_cond_init(voip_cond_t *out,void *attr)
{
    struct voip_cond *cond;
    (void)attr;
    if (!out) return EINVAL;
    *out=NULL;
    cond=calloc(1,sizeof(*cond));
    if (!cond) return ENOMEM;
    if (qosa_mutex_create(&cond->lock)) { free(cond); return ENOMEM; }
    *out=cond;
    return 0;
}
static int cond_wait(voip_cond_t *ref,qosa_mutex_t *mutex,uint32_t timeout)
{
    struct cond_waiter waiter={0}, **link;
    struct voip_cond *cond=ref ? *ref : NULL;
    int ret;
    if (!cond || !mutex) return EINVAL;
    if (qosa_sem_create(&waiter.sem,0)) return ENOMEM;
    qosa_mutex_lock(cond->lock,QOSA_WAIT_FOREVER);
    waiter.next=cond->waiters; cond->waiters=&waiter;
    qosa_mutex_unlock(*mutex);
    qosa_mutex_unlock(cond->lock);
    ret=qosa_sem_wait(waiter.sem,timeout);
    qosa_mutex_lock(cond->lock,QOSA_WAIT_FOREVER);
    for (link=&cond->waiters; *link && *link!=&waiter; link=&(*link)->next) {}
    if (*link) *link=waiter.next;
    if (waiter.signaled) ret=0;
    qosa_mutex_unlock(cond->lock);
    qosa_sem_delete(waiter.sem);
    qosa_mutex_lock(*mutex,QOSA_WAIT_FOREVER);
    return ret ? ETIMEDOUT : 0;
}
int voip_cond_wait(voip_cond_t *cond,qosa_mutex_t *mutex) { return cond_wait(cond,mutex,QOSA_WAIT_FOREVER); }
int voip_cond_timedwait(voip_cond_t *cond,qosa_mutex_t *mutex,const struct timespec *until)
{
    struct timespec now;
    int64_t ms;
    if (!until) return EINVAL;
    voip_clock_gettime(CLOCK_REALTIME,&now);
    ms=(int64_t)(until->tv_sec-now.tv_sec)*1000+(until->tv_nsec-now.tv_nsec)/1000000;
    return cond_wait(cond,mutex,ms<=0 ? 0 : ms>0xfffffffeULL ? 0xfffffffeU : (uint32_t)ms);
}
static int cond_signal(voip_cond_t *ref,bool all)
{
    struct voip_cond *cond=ref ? *ref : NULL;
    struct cond_waiter *waiter;
    if (!cond) return EINVAL;
    qosa_mutex_lock(cond->lock,QOSA_WAIT_FOREVER);
    while ((waiter=cond->waiters)!=NULL) {
        cond->waiters=waiter->next; waiter->signaled=true; qosa_sem_release(waiter->sem);
        if (!all) break;
    }
    qosa_mutex_unlock(cond->lock);
    return 0;
}
int voip_cond_signal(voip_cond_t *cond) { return cond_signal(cond,false); }
int voip_cond_broadcast(voip_cond_t *cond) { return cond_signal(cond,true); }
int voip_cond_destroy(voip_cond_t *ref)
{
    struct voip_cond *cond=ref ? *ref : NULL;
    if (!cond) return EINVAL;
    qosa_mutex_lock(cond->lock,QOSA_WAIT_FOREVER);
    if (cond->waiters) { qosa_mutex_unlock(cond->lock); return EBUSY; }
    qosa_mutex_unlock(cond->lock);
    qosa_mutex_delete(cond->lock); free(cond); *ref=NULL;
    return 0;
}

int voip_ipaddr_aton(const char *text, ip_addr_t *address)
{
    struct in_addr v4;
    if (!text || !address || inet_pton(AF_INET,text,&v4)!=1) return 0;
    memset(address,0,sizeof(*address));
    ip_2_ip4(address)->addr=v4.s_addr;
    IP_SET_TYPE_VAL(*address,IPADDR_TYPE_V4);
    return 1;
}
