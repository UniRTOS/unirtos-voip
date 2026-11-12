/* UniRTOS platform boundary for the imported ASR SIP/RTP implementation. */
#ifndef QCM_VOIP_PORT_H
#define QCM_VOIP_PORT_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "qosa_sys.h"
#include "qosa_rtc.h"
#include "qcm_voip_platform.h"
#include "qosa_sockets.h"
#include "qosa_log.h"

#ifndef NI_MAXHOST
#define NI_MAXHOST 256
#define NI_MAXSERV 16
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint32_t uint32;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef int QuecOSStatus;
typedef qosa_task_t quec_task_t;
typedef qosa_task_t OSATaskRef;
typedef qosa_sem_t quec_sem_t;
typedef qosa_msgq_t quec_queue_t;
typedef qosa_flag_t quec_flag_t;
typedef struct voip_timer *quec_timer_t;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define kNoErr 0
#define kNotInitializedErr (-1)
#define QUEC_WAIT_FOREVER QOSA_WAIT_FOREVER
#define QUEC_NO_WAIT QOSA_NO_WAIT
#define quec_rtos_get_systime qcm_voip_uptime_ms
#define quec_rtos_queue_create qosa_msgq_create
#define quec_rtos_queue_release qosa_msgq_release
#define quec_rtos_queue_wait qosa_msgq_wait
#define quec_rtos_semaphore_create qosa_sem_create
#define quec_rtos_semaphore_delete qosa_sem_delete
#define quec_rtos_semaphore_release qosa_sem_release
#define quec_rtos_semaphore_wait qosa_sem_wait
#define quec_rtos_task_sleep_ms qosa_task_sleep_ms
#define quec_rtos_task_suspend qosa_task_suspend
#define quec_rtos_task_delete qosa_task_delete
#define quec_rtos_task_create(ref,stack,prio,name,fn,arg) \
    qosa_task_create(ref,stack,QOSA_PRIORITY_NORMAL,name,fn,arg)
#define OSATaskCreate(ref,mem,stack,prio,name,fn,arg) \
    qosa_task_create(ref,stack,QOSA_PRIORITY_NORMAL,name,fn,arg)
#define osa_task_sleep_ms qosa_task_sleep_ms
#define osa_mutex_create qosa_mutex_create
#define osa_mutex_delete qosa_mutex_delete
#define osa_mutex_lock qosa_mutex_lock
#define osa_mutex_unlock qosa_mutex_unlock
#define osa_sem_create qosa_sem_create
#define osa_sem_delete qosa_sem_delete
#define osa_sem_wait qosa_sem_wait
#define osa_sem_release qosa_sem_release

int quec_rtos_timer_create(quec_timer_t *timer);
int voip_timer_start(quec_timer_t timer, uint32_t ms, bool repeat, void (*cb)(void *), void *arg);
#define quec_rtos_timer_start(timer,ms,repeat,cb,arg) \
    voip_timer_start(timer,ms,repeat,(void (*)(void *))(cb),(void *)(arg))
int quec_rtos_timer_stop(quec_timer_t timer);
int quec_rtos_timer_delete(quec_timer_t timer);
bool voip_timer_running(quec_timer_t timer);
uint64_t voip_monotonic_ms(void);
int voip_gettimeofday(struct timeval *tv, void *tz);
int voip_clock_gettime(int clock_id, struct timespec *ts);
int voip_nanosleep(const struct timespec *req, struct timespec *rem);
void voip_log_printf(const char *fmt, ...) __attribute__((format(printf,1,2)));
#define CPUartLogPrintf voip_log_printf
#define RTI_LOG voip_log_printf
#define diagPrintf voip_log_printf

/* This test port accepts IPv4; use exported socket conversion APIs. */
int voip_ipaddr_aton(const char *text, ip_addr_t *address);
#define ipaddr_aton voip_ipaddr_aton
#undef ipaddr_ntoa
#define ipaddr_ntoa(address) ip4addr_ntoa(ip_2_ip4(address))
#define TIME_VAL_DEF 1
#define gettimeofday voip_gettimeofday
#define clock_gettime voip_clock_gettime
#define nanosleep voip_nanosleep

/* Use component-private names to avoid redefining libc pthread/time types. */
typedef struct voip_thread *voip_thread_t;
typedef struct {uint32_t stackSize; uint8_t schedPriority, detachState; char *taskName;} voip_thread_attr_t;
typedef struct voip_cond *voip_cond_t;
int voip_thread_create(voip_thread_t *thread, const voip_thread_attr_t *attr, void *(*fn)(void *), void *arg);
int voip_thread_join(voip_thread_t thread, void **result);
unsigned long voip_thread_self(void);
int voip_cond_init(voip_cond_t *cond, void *attr);
int voip_cond_wait(voip_cond_t *cond, qosa_mutex_t *mutex);
int voip_cond_timedwait(voip_cond_t *cond, qosa_mutex_t *mutex, const struct timespec *until);
int voip_cond_signal(voip_cond_t *cond);
int voip_cond_broadcast(voip_cond_t *cond);
int voip_cond_destroy(voip_cond_t *cond);
static inline int voip_mutex_init(qosa_mutex_t *mutex, void *attr) { (void)attr; return qosa_mutex_create(mutex); }
static inline int voip_mutex_lock(qosa_mutex_t *mutex) { return qosa_mutex_lock(*mutex,QOSA_WAIT_FOREVER); }
static inline int voip_mutex_unlock(qosa_mutex_t *mutex) { return qosa_mutex_unlock(*mutex); }
static inline int voip_mutex_destroy(qosa_mutex_t *mutex) { return qosa_mutex_delete(*mutex); }
static inline int voip_attr_init(voip_thread_attr_t *attr) { memset(attr,0,sizeof(*attr)); attr->stackSize=8192; attr->taskName="voip_rtp"; return 0; }
static inline int voip_attr_destroy(voip_thread_attr_t *attr) { (void)attr; return 0; }
static inline int voip_attr_stack(voip_thread_attr_t *attr, size_t size) { attr->stackSize=size; return 0; }
#endif
