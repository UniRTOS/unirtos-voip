/*--------------------------------------------------------------------------------------------------------------------
(C) Copyright 2020 ASR Ltd. All Rights Reserved
-------------------------------------------------------------------------------------------------------------------*/
/* UniRTOS implementation: private pthread aliases, no ASR kernel dependencies. */
#ifndef QCM_VOIP_POSIX_H
#define QCM_VOIP_POSIX_H
#include "qcm_voip_port.h"
#define pthread_t voip_thread_t
#define pthread_attr_t voip_thread_attr_t
#define pthread_mutex_t qosa_mutex_t
#define pthread_cond_t voip_cond_t
#define pthread_create voip_thread_create
#define pthread_join voip_thread_join
#define pthread_self voip_thread_self
#define pthread_attr_init voip_attr_init
#define pthread_attr_destroy voip_attr_destroy
#define pthread_attr_setstacksize voip_attr_stack
#define pthread_mutex_init voip_mutex_init
#define pthread_mutex_lock voip_mutex_lock
#define pthread_mutex_unlock voip_mutex_unlock
#define pthread_mutex_destroy voip_mutex_destroy
#define pthread_cond_init voip_cond_init
#define pthread_cond_wait voip_cond_wait
#define pthread_cond_timedwait voip_cond_timedwait
#define pthread_cond_signal voip_cond_signal
#define pthread_cond_broadcast voip_cond_broadcast
#define pthread_cond_destroy voip_cond_destroy
#define POSIXTIMER_INTERVAL 10000
#define usleep(us) qosa_task_sleep_ms(((us)+999)/1000)
#define _time_spec timespec
#endif
