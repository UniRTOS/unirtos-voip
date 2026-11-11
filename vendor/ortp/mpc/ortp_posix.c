/*****************************************************************/ /**
* @file ortp_posix.c
* @brief This file provides the implementation of POSIX compatibility functions for the oRTP port on ASR 1903 platform.
* @author evin.zhu
* @date 2026-05-29
*
* @copyright Copyright (c) 2026 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
**********************************************************************/
//#include <stdlib.h>
//#include <string.h>
//#include "gbl_types.h"
#include "ortp_posix.h"
#include "diags.h"
//#include "msl_sal_stbc_type.h"
#include "csw_mem.h"
//#include "time.h"
#include "rtc.h"
#include "utltimer.h"

/*
 * POSIX compatibility helpers used by the oRTP port.
 * The VoIP stack calls pthread/semaphore style APIs, while the target
 * platform provides OSA primitives underneath.
 */
#define PRINTF(msg,args...)  CPUartLogPrintf("ortp[%d]"msg, __LINE__, ##args)

#define MAX_DXSLIC_PTHREADS 5

typedef void* (*ortp_entry_func)(void*);
typedef struct pthread_internal_t {
    OSTaskRef task_ref;
    void* stack;
    void* argv;
    pthread_attr_t attr;
    uint8_t exit_flag;
    ortp_entry_func func;
    OSATaskRef joined[MAX_DXSLIC_PTHREADS];
}pthread_internal_t;

#if !defined(SIMULATOR)
int errno = 0;
#endif

/* Map pthread mutex operations onto OSA semaphores for this port. */
 OSA_STATUS pthread_mutex_init(void*a, void*b) {
    OSASemaRef ref = *(OSASemaRef*)(a);
    return OSASemaphoreCreate((OSASemaRef*)a, 1, OSA_FIFO);
}

 OSA_STATUS pthread_mutex_lock(void*a) {
    OSASemaRef ref = *(OSASemaRef*)(a);
    if (ref) {
        return OSASemaphoreAcquire(*(OSASemaRef*)(a), OSA_SUSPEND);
    }
    else {
        PRINTF("%s,0x%lx is null!", __FUNCTION__, a);
        return OS_SUCCESS;
    }
}
 OSA_STATUS pthread_mutex_unlock(void*a) {
    OSASemaRef ref = *(OSASemaRef*)(a);
    if (ref) {
        UINT32 count = 0;
        OSASemaphorePoll(ref, &count);
        PRINTF("%s,0x%lx,count:%d", __FUNCTION__, a, count);
        if (count == 0)
            return OSASemaphoreRelease(*(OSASemaRef*)(a));
        else
            return OS_SUCCESS;
    }
    else {
        PRINTF("%s,0x%lx is null!", __FUNCTION__, a);
        return OS_SUCCESS;
    }
}

 OSA_STATUS pthread_mutex_destroy(void*a) {
    UINT32 count = 0;
    OSASemaRef ref = *(OSASemaRef*)(a);
    if (ref) {
        OSASemaphorePoll(ref, &count);
        PRINTF("%s,0x%lx,count:%d", __FUNCTION__, a, count);
        if (count == 0)
            OSASemaphoreRelease(ref);
        if (OSASemaphoreDelete(*(OSASemaRef*)(a)) == 0) {
            *(OSASemaRef*)(a) = 0;
        }

        return OS_SUCCESS;
    }
    else {
        PRINTF("%s,0x%lx is null!", __FUNCTION__, a);
        return OS_SUCCESS;
    }
}

/* Expose POSIX-like sem_* APIs on top of the same OSA semaphore objects. */
__POSIX_INTRINSIC OSA_STATUS sem_init(void*a, void*b, uint32_t c) {
    OSASemaRef ref = *(OSASemaRef*)(a);
    PRINTF("%s,0x%lx,0x%lx,initialCount:%d",  __FUNCTION__,  a, ref, c);
    return OSASemaphoreCreate((OSASemaRef*)a, c, OSA_FIFO);
}

__POSIX_INTRINSIC OSA_STATUS sem_wait(void*a) {
    OSASemaRef ref = *(OSASemaRef*)(a);
    if (ref) {
        PRINTF("%s,0x%lx", __FUNCTION__, a);
        return OSASemaphoreAcquire(*(OSASemaRef*)(a), OSA_SUSPEND);
    }
    else {
        PRINTF("%s,0x%lx is null!", __FUNCTION__, a);
        return OS_FAIL;
    }
}

__POSIX_INTRINSIC OSA_STATUS sem_post(void*a) {
    OSASemaRef ref = *(OSASemaRef*)(a);
    if (ref) {
        UINT32 count = 0;
        OSASemaphorePoll(*(OSASemaRef*)(a), &count);
        PRINTF("%s,0x%lx,count:%d", __FUNCTION__, a, count);
        if (count == 0)
            return OSASemaphoreRelease(*(OSASemaRef*)(a));
        else
            return OS_FAIL;
    }
    else {
        PRINTF("%s,0x%lx is null!", __FUNCTION__, a);
        return OS_FAIL;
    }
}

__POSIX_INTRINSIC OSA_STATUS sem_destroy(void*a) {
    OSASemaRef ref = *(OSASemaRef*)(a);
    PRINTF("%s,0x%lx,0x%lx",  __FUNCTION__,  a, ref);
    if (ref) {
        OSA_STATUS status = OSASemaphoreDelete(*(OSASemaRef*)(a));
        if (status == 0)
            *(OSASemaRef*)(a) = 0;
    }
    return OS_SUCCESS;
}

#define TIME_WAIT_RATE  1
__POSIX_INTRINSIC OSA_STATUS sem_timedwait(void*a, void*b) {
    struct _time_spec* ts = (struct _time_spec*)b;
    int32_t waited_ms = 0;
    int32_t target_ms = ts->tv_sec * 1000 + ts->tv_nsec / 1000 / 1000;
    PRINTF("%s,0x%lx,target_ms:%ld!", __FUNCTION__, a, target_ms);

    while (1) {
        if (OSASemaphoreAcquire(*(OSASemaRef*)(a), OSA_NO_SUSPEND) != 0) {
            OSATaskSleep(TIME_WAIT_RATE);
            waited_ms += (TICK2MS * TIME_WAIT_RATE);
        }
        else {
            break;
        }

        if (waited_ms >= target_ms) {
            PRINTF("%s,0x%lx,current:%ld,timeout!", __FUNCTION__, a, waited_ms);
            return -1;
        }
    }

    PRINTF("%s,0x%lx,current:%ld,ok!", __FUNCTION__, a, waited_ms);
    return 0;
}

/* Condition variables are implemented with semaphore wait/release behavior. */
 int _pthread_cond_timedwait(pthread_cond_t* a, pthread_mutex_t* mutex, const struct _time_spec* b) {
    struct _time_spec* ts = (struct _time_spec*)b;
    int32_t waited_ms = 0;
    int32_t target_ms = ts->tv_sec * 1000 + ts->tv_nsec / 1000 / 1000;
    UINT32 count = 0;
    OSASemaphorePoll(*(OSASemaRef*)(mutex), &count);
    PRINTF("%s,0x%lx,mutex:%d,target_ms:%ld!", __FUNCTION__, a, count, target_ms);

    while (1) {
        if (OSASemaphoreAcquire(*(OSASemaRef*)(a), OSA_NO_SUSPEND) != 0) {
            OSATaskSleep(TIME_WAIT_RATE);
            waited_ms += (TICK2MS * TIME_WAIT_RATE);
        }
        else {
            break;
        }

        if (waited_ms >= target_ms) {
            PRINTF("%s,0x%lx,current:%ld,timeout!", __FUNCTION__, a, waited_ms);
            return -1;
        }
    }

    PRINTF("%s,0x%lx,current:%ld,ok!", __FUNCTION__, a, waited_ms);
    return 0;
}

 int _pthread_cond_init(pthread_cond_t* cond, void* attr) {
    PRINTF("%s,0x%lx", __FUNCTION__, cond);
    return OSASemaphoreCreate((OSASemaRef*)cond, 1, OSA_FIFO);
}

 int _pthread_cond_destroy(pthread_cond_t *a) {
    PRINTF("%s,0x%lx", __FUNCTION__, a);
    OSA_STATUS status = OSASemaphoreDelete(*(OSASemaRef*)(a));
    if (status == 0) {
        *(OSASemaRef*)(a) = 0;
    }
    return status;
}

 int _pthread_cond_signal(pthread_cond_t *a) {
    UINT32 count = 0;
    OSASemaphorePoll(*(OSASemaRef*)(a), &count);
    PRINTF("%s,0x%lx,count:%d", __FUNCTION__, a, count);
    if (count == 0) {
        return OSASemaphoreRelease(*(OSASemaRef*)(a));
    }
    return OS_FAIL;
}

 int _pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex){
    UINT32 count = 0;
    OSASemaphorePoll(*(OSASemaRef*)(mutex), &count);
    PRINTF("%s,0x%lx,mutex:%d", __FUNCTION__, cond, count);
    
    if (OSASemaphoreAcquire(*(OSASemaRef*)(cond), OSA_SUSPEND) != 0) {
        return OS_FAIL;
    }
    return 0;    
}



int _pthread_cond_broadcast(pthread_cond_t *a){
    UINT32 count = 0;
    OSASemaphorePoll(*(OSASemaRef*)(a), &count);
    PRINTF("%s,0x%lx,count:%d", __FUNCTION__, a, count);
    if (count == 0) {
        return OSASemaphoreRelease(*(OSASemaRef*)(a));
    }
    return OS_FAIL;
}

static pthread_t dxslic_pthread_tbl[MAX_DXSLIC_PTHREADS] = { 0 };
void _pthread_free(int force) {
    int i = 0; 
    for (i = 0; i < MAX_DXSLIC_PTHREADS; i++) {
        pthread_internal_t* th = dxslic_pthread_tbl[i];
        if (th) {
            if (force || th->exit_flag) {
                DIAG_FILTER(AUDIO, dxslic_posix, _pthread_free, DIAG_INFORMATION)
                diagPrintf("delete task %s, %d", th->attr.taskName, force);
                OSATaskDelete(th->task_ref);
                alignFree(th->stack);
                free(th);
                dxslic_pthread_tbl[i] = 0;
            }
        }
    }
}

#if 0
int _sigaction(int signum, const struct _sigaction *act, struct _sigaction *oldact) {
    DIAG_FILTER(AUDIO, dxslic_posix, _sigaction, DIAG_INFORMATION)
    diagPrintf("signum:%d,act:0x%lx,oldact:0x%lx", signum, act, oldact);

    return 0;
}

int _timer_create(clockid_t clock_id, struct sigevent *evp, _timer_t *timerid) {
    int err_code = -1;
    if (evp && timerid) {
        memcpy(&timerid->sig, evp, sizeof(struct sigevent));
        err_code = OSATimerCreate(&timerid->timer_id);
        DIAG_FILTER(AUDIO, dxslic_posix, _timer_create, DIAG_INFORMATION)
        diagPrintf("timer_ref:0x%lx", timerid->timer_id);
    }

    return err_code;
}

int _timer_settime(_timer_t timerid, int flags,
    const struct _itimer_spec *new_value,
    struct _itimer_spec *old_value) {
    int err_code = -1;
    if (new_value && timerid.timer_id) {
        if (new_value->it_interval.tv_sec == 0
            && new_value->it_interval.tv_nsec == 0
            && new_value->it_value.tv_sec == 0
            && new_value->it_value.tv_nsec == 0) {
            DIAG_FILTER(AUDIO, dxslic_posix, timer_settime_stop, DIAG_INFORMATION)
            diagPrintf("timer_ref:0x%lx", timerid.timer_id);
            err_code = OSATimerStop(timerid.timer_id);
        }
        else {
            UINT32 initial = new_value->it_value.tv_sec * 1000 + new_value->it_value.tv_nsec / 1000000;
            UINT32 reshedule = new_value->it_interval.tv_sec * 1000 + new_value->it_interval.tv_nsec / 1000000;
            DIAG_FILTER(AUDIO, dxslic_posix, timer_settime_start, DIAG_INFORMATION)
            diagPrintf("timer_ref:0x%lx,initial:%ld,reshedule:%ld", timerid.timer_id, initial, reshedule);
            OSATimerStop(timerid.timer_id);
            err_code = OSATimerStart(timerid.timer_id, initial == 0 ? 1 : MS2TICK(initial), reshedule == 0 ? 0 : MS2TICK(reshedule), (void(*)(UINT32))timerid.sig.sigev_notify_function, 0);
        }
    }
    else {
        DIAG_FILTER(AUDIO, dxslic_posix, timer_settime_invalid, DIAG_INFORMATION)
        diagPrintf("new_value:0x%lx, timer_ref:0x%lx", new_value, timerid.timer_id);
    }

    return err_code;
}

int _timer_delete(_timer_t timerid) {
    DIAG_FILTER(AUDIO, dxslic_posix, _timer_delete, DIAG_INFORMATION)
    diagPrintf("timer_ref:0x%lx", timerid.timer_id);
    if (timerid.timer_id) {
        int error_code = 0;
        OSATimerStop(timerid.timer_id);
        error_code = OSATimerDelete(timerid.timer_id);
        timerid.timer_id = 0;
        return error_code;
    }
    return -1;
}

mqd_t _mq_open(const char *name, int oflag, int mode, struct _mq_attr *attr) {
    OSAMsgQRef ref = 0;
#ifdef OSA_QUEUE_NAMES
    OSAMsgQCreate(&ref, (char *)name, sizeof(MslMessage), attr->mq_maxmsg, OSA_PRIORITY);
#else
    OSAMsgQCreate(&ref, sizeof(MslMessage), attr->mq_maxmsg, OSA_PRIORITY);
#endif
    if(ref > 0)
        return (mqd_t)ref;
    return 0;
}

int _mq_receive(mqd_t mqdes, char *msg_ptr, unsigned int msg_len, unsigned *msg_prio) {
    UINT32 count = 0;
    OSAMsgQPoll(mqdes, &count);
    if (count > 0) {
        MslMessage msg = { 0 };
        OS_STATUS status = OSAMsgQRecv(mqdes, (UINT8*)&msg, sizeof(msg), OSA_NO_SUSPEND);
        if (status == OS_SUCCESS && msg_ptr && msg.pparm) {
            ASSERT(msg_len == msg.value);
            memcpy(msg_ptr, msg.pparm, msg_len);
            free(msg.pparm);
            return msg_len;
        }
    }
    return -1;
}

//extern void SLICAPP_RIL_Handler(UINT32 cookie);
int _mq_send(mqd_t mqdes, const char *msg_ptr, unsigned int msg_len, unsigned int msg_prio) {
    MslMessage msg = { 0 };
    OS_STATUS status;
    void* ppram = malloc(msg_len);
    ASSERT(ppram);
    memcpy(ppram, msg_ptr, msg_len);
    msg.pparm = ppram;
    msg.value = msg_len;
    status = OSAMsgQSend(mqdes, sizeof(msg), (UINT8*)&msg, OS_NO_SUSPEND);
    if (status == OS_SUCCESS) {
       // SLICAPP_RIL_Handler(0);
        return 0;
    }
        
    return -1;
}
#endif
extern struct tm GetLocalTime(void);
void _clock_gettime(int type, struct _time_spec* local) {
    if (type == CLOCK_REALTIME) {
        local->tv_sec = 0;
        local->tv_nsec = 0;
    }
}

int _nanosleep(const struct _time_spec* ts, void* resv) {
    usleep(ts->tv_sec*1000000 + ts->tv_nsec/1000);
    return 0;
}

int _pthread_attr_init(pthread_attr_t* attr) {
    if (!attr->stackSize)
        attr->stackSize = 5 * 1024;
    if (!attr->schedPriority)
        attr->schedPriority = 100;
    attr->detachState = 0;

    return 0;
}

int _pthread_attr_destroy(pthread_attr_t* attr) {
    return 0;
}

unsigned long _pthread_self(void)
{
    OSATaskRef task_ref=NULL;
    unsigned long ret=0;
    char i=0;
    pthread_internal_t* pthread_impl=NULL;
    OSATaskGetCurrentRef(&task_ref);

    for (i = 0; i < MAX_DXSLIC_PTHREADS; i++) 
    {
        pthread_impl=(pthread_internal_t*)dxslic_pthread_tbl[i];
        if (pthread_impl&&pthread_impl->task_ref==task_ref) 
        {
            ret=(unsigned int)pthread_impl;
            break;
        }
    }    
	return ret;
}

void ortp_posix_main(void *argv)
{
pthread_internal_t* pthread_impl=(pthread_internal_t*)argv;
int i = 0;

for (i = 0; i < MAX_DXSLIC_PTHREADS; i++) {
    if (!dxslic_pthread_tbl[i]) {
        dxslic_pthread_tbl[i] = (pthread_t)pthread_impl;
        break;
    }
}
    OSATaskSleep(5);
    pthread_impl->func(pthread_impl->argv);

}
 
int _pthread_create(pthread_t* thread, pthread_attr_t* attr, void* (* entry)(void *), void* argv) {
    OSA_STATUS status = OS_FAIL;
    void* stack = alignMalloc(attr->stackSize);
    DIAG_FILTER(AUDIO, dxslic_posix, _pthread_create, DIAG_INFORMATION)
    diagPrintf("%s,schedPriority:%d,stackSize:%d", attr->taskName, attr->schedPriority, attr->stackSize);
    pthread_internal_t* pthread_impl = (pthread_internal_t*)malloc(sizeof(pthread_internal_t));
    if (pthread_impl && stack) {
        memset(pthread_impl, 0, sizeof(pthread_internal_t));
        pthread_impl->func=entry;
        DIAG_FILTER(AUDIO, dxslic_posix, _pthread_created, DIAG_INFORMATION)
        diagPrintf("%s,task_ref:0x%lx", attr->taskName, pthread_impl->task_ref);
        memcpy(&pthread_impl->attr, attr, sizeof(pthread_attr_t));
        pthread_impl->stack = stack;
        pthread_impl->argv=argv;
        status = OSATaskCreate(&pthread_impl->task_ref, stack, attr->stackSize, attr->schedPriority, attr->taskName, ortp_posix_main, (void *)pthread_impl);
        if (status == OS_SUCCESS) {
            *thread=(pthread_t*)pthread_impl;
            return OS_SUCCESS;
        }
        else
        {
            free(pthread_impl);
        }
    }

    if (stack)
        alignFree(stack);
    if (pthread_impl)
        free(pthread_impl);

    return status;
}

int _pthread_join(pthread_t thread, void **retval) {
    pthread_internal_t* th = (pthread_internal_t*)thread;
    OSATaskRef task_ref = 0;
    OSATaskGetCurrentRef(&task_ref);
    DIAG_FILTER(AUDIO, dxslic_posix, _pthread_join, DIAG_INFORMATION)
    diagPrintf("context:0x%lx,0x%lx(name:%s,detached:%d)", task_ref, th->task_ref, th->attr.taskName, th->attr.detachState);
   
    if (th && !th->attr.detachState) {
        int i = 0;
        for (i = 0; i < MAX_DXSLIC_PTHREADS; i++) {
            if (dxslic_pthread_tbl[i]) {
                pthread_internal_t* ith = dxslic_pthread_tbl[i];
                if (ith == th) {
                    int j = 0;
                    for (j = 0; j < MAX_DXSLIC_PTHREADS; j++) {
                        if (!ith->joined[j]) {
                            int* ret = (int*)retval;
                            ith->joined[j] = task_ref;
                            DIAG_FILTER(AUDIO, dxslic_posix, _pthread_suspend_joined, DIAG_INFORMATION)
                            diagPrintf("task_ref:0x%lx", th->joined[j]);
                            OSATaskSuspend(task_ref);
                            if (ret)
                                *ret = 0;
                            return 0;
                        }
                    }
                }
            }
        }
    }

    return -1;
}

void _pthread_exit(void* retval) {
    OSATaskRef task_ref = 0;
    OSA_STATUS status = OSATaskGetCurrentRef(&task_ref);
    DIAG_FILTER(AUDIO, dxslic_posix, _pthread_exit, DIAG_INFORMATION)
    diagPrintf("task_ref:0x%lx", task_ref);

    if (status == OS_SUCCESS) {
        int i = 0;
        for (i = 0; i < MAX_DXSLIC_PTHREADS; i++) {
            if (dxslic_pthread_tbl[i]) {
                pthread_internal_t* th = dxslic_pthread_tbl[i];
                if (th && th->task_ref == task_ref) {
                    int j = 0;
                    int* ret = (int*)retval;
                    if (ret)
                        *ret = 0;
                    th->exit_flag = 1;
                    for (j = 0; j < MAX_DXSLIC_PTHREADS; j++) {
                        if (th->joined[j]) {
                            DIAG_FILTER(AUDIO, dxslic_posix, _pthread_resume_joined, DIAG_INFORMATION)
                            diagPrintf("task_ref:0x%lx", th->joined[j]);
                            OSATaskResume(th->joined[j]);
                            th->joined[j] = 0;
                        }
                    }
                    break;
                }
            }
        }
    }
}

int _pthread_detach(pthread_t thread) {
    pthread_internal_t* th = (pthread_internal_t*)thread;
    int error_code = -1;
    OSATaskRef task_ref = 0;
    OSATaskGetCurrentRef(&task_ref);
    DIAG_FILTER(AUDIO, dxslic_posix, _pthread_detach, DIAG_INFORMATION)
    diagPrintf("context:0x%lx,0x%lx(name:%s,detached:%d)", task_ref, th->task_ref, th->attr.taskName, th->attr.detachState);

    if (th && !th->attr.detachState) {
        int i = 0;
        for (i = 0; i < MAX_DXSLIC_PTHREADS; i++) {
            if (dxslic_pthread_tbl[i]) {
                pthread_internal_t* ith = dxslic_pthread_tbl[i];
                if (ith == th) {
                    th->attr.detachState = 1;
                    error_code = 0;
                    break;
                }
            }
        }
    }

    return error_code;
}

int64_t timespec_to_nanosecond(const struct timespec *value)
{
    return ((int64_t)value->tv_sec * NANOSECONDS_PER_SECOND + value->tv_nsec);
}

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
    int64_t time_ns = 0;

    if (rqtp == NULL) {
        return -1;
    }

    time_ns = timespec_to_nanosecond(rqtp);
    OsaTaskSleep((time_ns * POSIX_TIMER_TICKS_PER_SECOND) / NANOSECONDS_PER_SECOND, NULL);

    return 0;
}

struct timespec nanosecond_to_timespec(int64_t value)
{
    struct timespec time;

    time.tv_sec = value / NANOSECONDS_PER_SECOND;
    time.tv_nsec = value % NANOSECONDS_PER_SECOND;

    return time;
}

static int64_t g_realtime_clock_base = 0;

int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    uint64_t time_ns = 0;

    if (tp == NULL) {
        return -1;
    }

    time_ns = (OsaGetTicks(NULL) * NANOSECONDS_PER_SECOND) / POSIX_TIMER_TICKS_PER_SECOND;

    if (clock_id == CLOCK_MONOTONIC) {
        *tp = nanosecond_to_timespec(time_ns);
    } else if (clock_id == CLOCK_REALTIME) {
        *tp = nanosecond_to_timespec(g_realtime_clock_base + time_ns);
    } else {
        return -1;
    }

    return 0;
}

