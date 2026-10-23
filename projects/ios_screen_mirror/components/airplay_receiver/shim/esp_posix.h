// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define THREADS_H
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include "lwip/sockets.h"
#include "lwip/tcp.h"
int airplay_thread_create(pthread_t *thread, void *(*entry)(void *), void *arg);
typedef pthread_t thread_handle_t;
typedef pthread_mutex_t mutex_handle_t;
typedef pthread_cond_t cond_handle_t;
#define THREAD_RETVAL void *
#define THREAD_CREATE(h, f, a) do { if (airplay_thread_create(&(h), f, a)) (h) = 0; } while (0)
#define THREAD_JOIN(h) pthread_join(h, NULL)
#define MUTEX_CREATE(h) pthread_mutex_init(&(h), NULL)
#define MUTEX_LOCK(h) pthread_mutex_lock(&(h))
#define MUTEX_UNLOCK(h) pthread_mutex_unlock(&(h))
#define MUTEX_DESTROY(h) pthread_mutex_destroy(&(h))
#define COND_CREATE(h) pthread_cond_init(&(h), NULL)
#define COND_SIGNAL(h) pthread_cond_signal(&(h))
#define COND_DESTROY(h) pthread_cond_destroy(&(h))
#define sleepms(ms) usleep((ms) * 1000)
#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif
