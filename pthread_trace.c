#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "utils.h"
#include "expect.h"

#if !defined (__linux__) || !defined(__GLIBC__)
#error "This stuff only works on Linux!"
#endif

static int (*real_pthread_create)(pthread_t *, const pthread_attr_t *, void *(*) (void *), void *);
static int (*real_pthread_join)(pthread_t, void **);

static int (*real_pthread_mutex_lock)(pthread_mutex_t *);
static int (*real_pthread_mutex_trylock)(pthread_mutex_t *);
static int (*real_pthread_mutex_unlock)(pthread_mutex_t *);

static int (*real_pthread_barrier_init)(
            pthread_barrier_t *restrict, 
            const pthread_barrierattr_t *restrict,
            unsigned);
static int (*real_pthread_barrier_destroy)(pthread_barrier_t *);
static int (*real_pthread_barrier_wait)(pthread_barrier_t *);

static int (*real_pthread_cond_destroy)(pthread_cond_t *);
static int (*real_pthread_cond_init)(pthread_cond_t *restrict, const pthread_condattr_t *restrict);
static int (*real_pthread_cond_broadcast)(pthread_cond_t *);
static int (*real_pthread_cond_signal)(pthread_cond_t *);

/* Function declarations */
static void init(void) __attribute ((constructor));
static void fini(void) __attribute ((destructor));

static pid_t
gettid(void)
{
    return syscall(SYS_gettid);
}

#define LOG(fmt, args...) do {                          \
    fprintf(stderr, "== (%d, %d) %s(" fmt ")\n",        \
            getpid(), gettid(), __FUNCTION__, ##args);  \
} while (0)

#define LOAD_FUNC(name) do {                            \
    *(void**) (&real_##name) = dlsym(RTLD_NEXT, #name); \
    assert(real_##name);                                \
} while (0)                                             \

static void
init(void)
{
    LOAD_FUNC(pthread_create);
    LOAD_FUNC(pthread_join);

    LOAD_FUNC(pthread_mutex_lock);
    LOAD_FUNC(pthread_mutex_trylock);
    LOAD_FUNC(pthread_mutex_unlock);

    LOAD_FUNC(pthread_barrier_init);
    LOAD_FUNC(pthread_barrier_destroy);
    LOAD_FUNC(pthread_barrier_wait);

    LOAD_FUNC(pthread_cond_init);
    LOAD_FUNC(pthread_cond_destroy);
    LOAD_FUNC(pthread_cond_broadcast);
    LOAD_FUNC(pthread_cond_signal);
}

static void
fini(void)
{
}

int
pthread_create(pthread_t *newthread, const pthread_attr_t *attr,
        void *(*start_routine) (void *), void *arg)
{
    LOG("");
    return real_pthread_create(newthread, attr, start_routine, arg); 
}

int
pthread_join(pthread_t thread, void **retval)
{
    LOG("");
    return real_pthread_join(thread, retval);
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    LOG("mutex = %#lx", (long)mutex);
    return real_pthread_mutex_lock(mutex);
}

int
pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    LOG("mutex = %#lx", (long)mutex);
    return real_pthread_mutex_trylock(mutex);
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    LOG("mutex = %#lx", (long)mutex);
    return real_pthread_mutex_unlock(mutex);
}

int
pthread_barrier_init(pthread_barrier_t *restrict barrier,
                     const pthread_barrierattr_t *restrict attr,
                     unsigned count)
{
    LOG("barrier = %#lx, count = %u", (long)barrier, count);
    return real_pthread_barrier_init(barrier, attr, count);
}

int
pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    LOG("barrier = %#lx", (long)barrier);
    return real_pthread_barrier_destroy(barrier);
}

int
pthread_barrier_wait(pthread_barrier_t *barrier)
{
    LOG("barrier = %#lx", (long)barrier);
    return real_pthread_barrier_wait(barrier);
}


int
pthread_cond_destroy(pthread_cond_t *cond)
{
    LOG("cond = %#lx", (long)cond);
    return real_pthread_cond_destroy(cond);
}

int
pthread_cond_init(pthread_cond_t *restrict cond,
                  const pthread_condattr_t *restrict attr)
{
    LOG("cond = %#lx", (long)cond);
    return real_pthread_cond_init(cond, attr);
}

int
pthread_cond_broadcast(pthread_cond_t *cond)
{
    LOG("cond = %#lx", (long)cond);
    return real_pthread_cond_broadcast(cond);
}

int
pthread_cond_signal(pthread_cond_t *cond)
{
    LOG("cond = %#lx", (long)cond);
    return real_pthread_cond_signal(cond);
}


