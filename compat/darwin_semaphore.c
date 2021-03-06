/*
 * Copyright (C) 2000,02 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 * Written by Ga<EB>l Le Mignot <address@hidden>
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the GNU C Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "darwin_semaphore.h"

#include <assert.h>
#include <errno.h>
#include <sys/types.h>

#define __SEM_ID_NONE  0x0
#define __SEM_ID_LOCAL 0xcafef00d

/* http://www.opengroup.org/onlinepubs/007908799/xsh/sem_init.html */
int
compat_sem_init(compat_sem_t *sem, int pshared, unsigned int value)
{
    if (pshared) {
        errno = ENOSYS;
        return -1;
    }

    sem->id = __SEM_ID_NONE;

    if (pthread_cond_init(&sem->__data.local.count_cond, NULL)) {
        goto cond_init_fail;
    }

    if (pthread_mutex_init(&sem->__data.local.count_lock, NULL)) {
        goto mutex_init_fail;
    }

    sem->__data.local.count = value;
    sem->id = __SEM_ID_LOCAL;

    return 0;

mutex_init_fail:

    pthread_cond_destroy(&sem->__data.local.count_cond);

cond_init_fail:

    return -1;
}

/* http://www.opengroup.org/onlinepubs/007908799/xsh/sem_destroy.html */
int
compat_sem_destroy(compat_sem_t *sem)
{
    int res = 0;

    pthread_mutex_lock(&sem->__data.local.count_lock);

    sem->id = __SEM_ID_NONE;
    pthread_cond_broadcast(&sem->__data.local.count_cond);

    if (pthread_cond_destroy(&sem->__data.local.count_cond)) {
        res = -1;
    }

    pthread_mutex_unlock(&sem->__data.local.count_lock);

    if (pthread_mutex_destroy(&sem->__data.local.count_lock)) {
        res = -1;
    }

    return res;
}

int
compat_sem_getvalue(compat_sem_t *sem, unsigned int *sval)
{
    int res = 0;

    pthread_mutex_lock(&sem->__data.local.count_lock);

    if (sem->id != __SEM_ID_LOCAL) {
        res = -1;
        errno = EINVAL;
    } else {
        *sval = sem->__data.local.count;
    }

    pthread_mutex_unlock(&sem->__data.local.count_lock);

    return res;
}

/* http://www.opengroup.org/onlinepubs/007908799/xsh/sem_post.html */
int
compat_sem_post(compat_sem_t *sem)
{
    int res = 0;

    pthread_mutex_lock(&sem->__data.local.count_lock);

    if (sem->id != __SEM_ID_LOCAL) {
        res = -1;
        errno = EINVAL;
    } else if (sem->__data.local.count < COMPAT_SEM_VALUE_MAX) {
        sem->__data.local.count++;
	if (sem->__data.local.count == 1) {
            pthread_cond_signal(&sem->__data.local.count_cond);
        }
    } else {
        errno = ERANGE;
        res = -1;
    }

    pthread_mutex_unlock(&sem->__data.local.count_lock);

    return res;
}

/* http://www.opengroup.org/onlinepubs/009695399/functions/sem_timedwait.html */
int
compat_sem_timedwait(compat_sem_t *sem, const struct timespec *abs_timeout)
{
    int res = 0;

    if (abs_timeout &&
        (abs_timeout->tv_nsec < 0 || abs_timeout->tv_nsec >= 1000000000)) {
       errno = EINVAL;
       return -1;
    }

    pthread_cleanup_push((void(*)(void*))&pthread_mutex_unlock,
                         &sem->__data.local.count_lock);

    pthread_mutex_lock(&sem->__data.local.count_lock);

    if (sem->id != __SEM_ID_LOCAL) {
        errno = EINVAL;
        res = -1;
    } else {
        if (!sem->__data.local.count) {
            res = pthread_cond_timedwait(&sem->__data.local.count_cond,
                                         &sem->__data.local.count_lock,
                                         abs_timeout);
        }
        if (res) {
            assert(res == ETIMEDOUT);
            res = -1;
            errno = ETIMEDOUT;
        } else if (sem->id != __SEM_ID_LOCAL) {
	    res = -1;
            errno = EINVAL;
	} else {
            sem->__data.local.count--;
        }
    }

    pthread_cleanup_pop(1);

    return res;
}

/* http://www.opengroup.org/onlinepubs/007908799/xsh/sem_trywait.html */
int
compat_sem_trywait(compat_sem_t *sem)
{
    int res = 0;

    pthread_mutex_lock(&sem->__data.local.count_lock);

    if (sem->id != __SEM_ID_LOCAL) {
        res = -1;
        errno = EINVAL;
    } else if (sem->__data.local.count) {
        sem->__data.local.count--;
    } else {
        res = -1;
        errno = EAGAIN;
    }

    pthread_mutex_unlock (&sem->__data.local.count_lock);

    return res;
}

/* http://www.opengroup.org/onlinepubs/007908799/xsh/sem_wait.html */
int
compat_sem_wait(compat_sem_t *sem)
{
    int res = 0;

    pthread_cleanup_push((void(*)(void*))&pthread_mutex_unlock,
                          &sem->__data.local.count_lock);

    pthread_mutex_lock(&sem->__data.local.count_lock);

    if (sem->id != __SEM_ID_LOCAL) {
        errno = EINVAL;
        res = -1;
    } else {
        while (!sem->__data.local.count) {
            pthread_cond_wait(&sem->__data.local.count_cond,
                              &sem->__data.local.count_lock);
        }
        if (sem->id != __SEM_ID_LOCAL) {
	    res = -1;
            errno = EINVAL;
	} else {
            sem->__data.local.count--;
        }
    }

    pthread_cleanup_pop(1);

    return res;
}
