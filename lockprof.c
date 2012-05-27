#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <glib.h>
#include <errno.h>

#include "tsc.h"
#include "log.h"
#include "expect.h"

#if !defined (__linux__) || !defined(__GLIBC__)
#error "This stuff only works on Linux!"
#endif

#define AGGREGATE

static int (*real_pthread_create)(pthread_t *, const pthread_attr_t *,
				  void *(*) (void *), void *);

static int (*real_pthread_mutex_lock)(pthread_mutex_t *);
static int (*real_pthread_mutex_unlock)(pthread_mutex_t *);
static int (*real_pthread_barrier_wait)(pthread_barrier_t *);
static int (*real_pthread_rwlock_rdlock)(pthread_rwlock_t *);
static int (*real_pthread_rwlock_wrlock)(pthread_rwlock_t *);

/* No need to do anything with them now */
// static int (*real_pthread_mutex_trylock)(pthread_mutex_t *);

//static int (*real_pthread_barrier_init)(pthread_barrier_t *restrict,
//					const pthread_barrierattr_t *restrict,
//					unsigned);
//static int (*real_pthread_rwlock_init)(pthread_rwlock_t * restrict,
//				       const pthread_rwlockattr_t * restrict);

/* Unsupported, exist if any of these is used */
static int (*real_pthread_cond_init)(pthread_cond_t *restrict,
				     const pthread_condattr_t *restrict);

/* Function declarations */
static void init(void) __attribute ((constructor));
static void fini(void) __attribute ((destructor));

typedef struct thread_info_s {
    void *(*start_routine) (void *);
    void *arg;
    int core;

#ifdef AGGREGATE
    uint64_t mutex_spin;
    uint64_t barrier_spin;
    uint64_t rwlock_spin;

    uint64_t mutex_spin_local;
    uint64_t rwlock_spin_local;
#else
    GArray *timestamp;
#endif /* AGGREGATE */

    uint64_t tsc_start;
    uint64_t tsc_end;

    uint64_t arrival_no_spinning;

    uint64_t tsc_overhead;
} thread_info_t;

/* Global variables */
static int nthreads;
static uint64_t total_xxx;

#define MAX_PROFILED_THREADS 128
static struct thread_info_s thread_info_vect[MAX_PROFILED_THREADS];
static __thread struct thread_info_s *thread_info;

#define THREAD_TO_CORE_MAP_SIZE 4
static int thread_to_core_map[THREAD_TO_CORE_MAP_SIZE] = {1, 3, 5, 7};

#define LOAD_FUNC(name) do {                            \
    *(void**) (&real_##name) = dlsym(RTLD_NEXT, #name); \
    assert(real_##name);                                \
} while (0)

static void
init(void)
{
    LOAD_FUNC(pthread_create);
    LOAD_FUNC(pthread_mutex_lock);
    LOAD_FUNC(pthread_mutex_unlock);
    LOAD_FUNC(pthread_barrier_wait);
    LOAD_FUNC(pthread_rwlock_rdlock);
    LOAD_FUNC(pthread_rwlock_wrlock);

    LOAD_FUNC(pthread_cond_init);

#ifndef AGGREGATE
    for (int i = 0; i < MAX_PROFILED_THREADS; i++)
	thread_info_vect[i].timestamp = g_array_new(FALSE, FALSE,
						    sizeof(uint64_t));
#endif /* AGGREGATE */
}

static void
fini(void)
{
    log_t log;
    log_header_t header;
    log_event_t event;
    const char *ofile, *e;

    ofile = "foo.log";
    if ((e = getenv("PREPROF_FILE")))
        ofile = e;

    /* Initialize log header */
    header.version = LOG_VERSION_CURRENT;

    for (int i = 0; i < nthreads; i++) {
        /* initialize header */
        if (header.num_processes < LOG_MAX_PROCS) {
            log_header_process_t *h =
                &header.processes[header.num_processes++];
            h->pmc_map.counters = 0;
            h->pmc_map.offcore_rsp0 = 0;
            h->pmc_map.ireset = 0;
        }
    }

    /* Write event to log file */
    EXPECT_EXIT(log_create(&log, &header, ofile) == LOG_ERROR_OK);

    /* FIXME: quite dirty - but we profile only applications with threads that
       execute the same code */

#ifdef AGGREGATE
#define DUMP_EVENT(lock_type)						\
    do {								\
	memset(&event, 0, sizeof(event));				\
	for (int i = 0; i < nthreads; i++) {				\
	    struct thread_info_s *_thread = &thread_info_vect[i];	\
									\
	    event.pmc[event.num_processes++].tsc =			\
		_thread->lock_type ## _spin;				\
	}								\
	EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);     \
    } while (0)
    DUMP_EVENT(mutex);
    DUMP_EVENT(barrier);
    DUMP_EVENT(rwlock);
#undef DUMP_EVENT
#else
    int len = thread_info_vect[0].timestamp->len;
    for (int j = 0; j < len; j++) {
	log_event_t event;
	memset(&event, 0, sizeof(event));
	for (int i = 0; i < nthreads; i++) {
	    GArray *timestamp = thread_info_vect[i].timestamp;

	    event.pmc[event.num_processes++].tsc =
		g_array_index(timestamp, uint64_t, j);
	    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);
	}
    }
#endif /* AGGREGATE */

    uint64_t min_tsc_start = UINT64_C(-1);
    uint64_t max_tsc_end = UINT64_C(0);
    for (int i = 0; i < nthreads; i++) {
       if (max_tsc_end < thread_info_vect[i].tsc_end)
           max_tsc_end = thread_info_vect[i].tsc_end;
        if (min_tsc_start > thread_info_vect[i].tsc_start)
           min_tsc_start = thread_info_vect[i].tsc_start;
    }

    memset(&event, 0, sizeof(event));
    for (int i = 0; i < nthreads; i++) {
        event.pmc[event.num_processes++].tsc =
            thread_info_vect[i].tsc_start - min_tsc_start;
    }
    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);

    memset(&event, 0, sizeof(event));
    for (int i = 0; i < nthreads; i++) {
        event.pmc[event.num_processes++].tsc =
            max_tsc_end - thread_info_vect[i].tsc_end;
    }
    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);

    EXPECT_EXIT(log_close(&log) == LOG_ERROR_OK);

#ifndef AGGREGATE
    for (int i = 0; i < MAX_PROFILED_THREADS; i++)
	g_array_free(thread_info_vect[i].timestamp, FALSE);
#endif /* AGGREGATE */

    for (int i = 0; i < nthreads; i++) {
        struct thread_info_s *t = &thread_info_vect[i];

        printf("lockprof: thread %d: overhead: %lu\n",
                i, t->tsc_overhead);
    }

    for (int i = 0; i < nthreads; i++) {
        struct thread_info_s *t = &thread_info_vect[i];

        printf("lockprof: thread %d: cycles: %lu\n",
                i, t->tsc_end - t->tsc_start - t->tsc_overhead);
    }

    printf("lockprof: total_xxx: %lu\n", total_xxx);
}

static void *
_start_routine(void *arg)
{
    void *res;
    thread_info = arg;

    thread_info->tsc_start = read_tsc_p();
    res = (*thread_info->start_routine)(thread_info->arg);
    thread_info->tsc_end = read_tsc_p();

    return res;
}

int
pthread_create(pthread_t *newthread, const pthread_attr_t *attr,
	       void *(*start_routine) (void *), void *arg)
{
    cpu_set_t set;
    struct thread_info_s *_thread_info; 

    CPU_ZERO(&set);
    CPU_SET(thread_to_core_map[nthreads % THREAD_TO_CORE_MAP_SIZE], &set);
    EXPECT_EXIT(!pthread_setaffinity_np(pthread_self(), sizeof(set), &set));

    _thread_info = &thread_info_vect[nthreads];
    _thread_info->start_routine = start_routine;
    _thread_info->arg = arg;

    ++nthreads;

    return real_pthread_create(newthread, attr, _start_routine, _thread_info);
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    res = real_pthread_mutex_lock(mutex);
    tsc_diff = read_tsc_p() - tsc1;

#ifdef AGGREGATE
    thread_info->mutex_spin_local += tsc_diff;
#else
    g_array_append_val(thread_info->timestamp, tsc_diff);
#endif /* AGGREGATE */

    return res;
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    return real_pthread_mutex_unlock(mutex);
}

int
pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    res = real_pthread_rwlock_rdlock(rwlock);
    tsc_diff = read_tsc_p() - tsc1;
#ifdef AGGREGATE
    thread_info->rwlock_spin_local += tsc_diff;
#else
    g_array_append_val(thread_info->timestamp, tsc_diff);
#endif /* AGGREGATE */

    return res;
}

int
pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    res = real_pthread_rwlock_wrlock(rwlock);
    tsc_diff = read_tsc_p() - tsc1;
#ifdef AGGREGATE
    thread_info->rwlock_spin_local += tsc_diff;
#else
    g_array_append_val(thread_info->timestamp, tsc_diff);
#endif /* AGGREGATE */


    return res;
}

int
pthread_barrier_wait(pthread_barrier_t *barrier)
{
    int i, res;
    uint64_t tsc1, tsc2;

    tsc1 = read_tsc_p();
    res = real_pthread_barrier_wait(barrier);
    tsc2 = read_tsc_p();

#ifdef AGGREGATE
    thread_info->barrier_spin += tsc2 - tsc1;

    thread_info->mutex_spin += thread_info->mutex_spin_local;
    thread_info->rwlock_spin += thread_info->rwlock_spin_local;

    thread_info->arrival_no_spinning = tsc1 - 
        (thread_info->mutex_spin_local +
         thread_info->rwlock_spin_local);

    if (res == PTHREAD_BARRIER_SERIAL_THREAD) {
        thread_info_t *t = &thread_info_vect[0];
        uint64_t spin = t->mutex_spin_local + t->rwlock_spin_local;
        uint64_t max_arrival_no_spinning = t->arrival_no_spinning;

        for (i = 1; i < nthreads; i++) {
            t = &thread_info_vect[i];

            if (t->arrival_no_spinning > max_arrival_no_spinning) {
                max_arrival_no_spinning = t->arrival_no_spinning;
                spin = t->mutex_spin_local + t->rwlock_spin_local;
            }
        }

        total_xxx += spin;
    }

    thread_info->mutex_spin_local = 0;
    thread_info->rwlock_spin_local = 0;

    real_pthread_barrier_wait(barrier);
    thread_info->tsc_overhead += read_tsc_p() - tsc2;
#else
    g_array_append_val(thread_info->timestamp, tsc2 - tsc1);
#endif /* AGGREGATE */

    return res;
}

int
pthread_cond_init(pthread_cond_t *restrict cond,
                  const pthread_condattr_t *restrict attr)
{
    fprintf(stderr, "%s invoked. Exiting now.\n", __FUNCTION__);
    exit(1);

    return 0;
}
