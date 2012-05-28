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
#include <numa.h>

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
static int (*real_pthread_barrier_wait)(pthread_barrier_t *);
static int (*real_pthread_rwlock_rdlock)(pthread_rwlock_t *);
static int (*real_pthread_rwlock_wrlock)(pthread_rwlock_t *);

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
#else
    GArray *timestamp;
#endif /* AGGREGATE */

    uint64_t tsc_start;
    uint64_t tsc_end;
} thread_info_t;

/* Global variables */
static int nthreads;

static int opt_null_sync;

#define MAX_PROFILED_THREADS 128
static struct thread_info_s thread_info_vect[MAX_PROFILED_THREADS];
static __thread struct thread_info_s *thread_info;

#define THREAD_INFO_VECT_FOREACH(_iter) \
    for (int i = 0; i < nthreads && (_iter = &thread_info_vect[i]); i++)

#define THREAD_TO_CORE_MAP_SIZE 4
static int thread_to_core_map[THREAD_TO_CORE_MAP_SIZE] = {1, 3, 5, 7};
static int numa_node = 1;

#define LOAD_FUNC(name) do {                            \
    *(void**) (&real_##name) = dlsym(RTLD_NEXT, #name); \
    assert(real_##name);                                \
} while (0)

static int
set_core_affinity(int core)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(core, &set);

    EXPECT(!pthread_setaffinity_np(pthread_self(), sizeof set, &set));
    return 0;
}

static int
set_numa_affinity(int node)
{
    struct bitmask *nm;

    EXPECT(!numa_available());
    EXPECT(numa_bitmask_isbitset(numa_get_mems_allowed(), node));

    EXPECT((nm = numa_allocate_nodemask()) != NULL);
    numa_bitmask_clearall(nm);
    numa_bitmask_setbit(nm, node);
    numa_set_membind(nm);
    numa_free_nodemask(nm);
    return 0;
}

static void
init(void)
{
    const char *e;

    if ((e = getenv("PREPROF_NULL_SYNC")))
        opt_null_sync = 1;

    LOAD_FUNC(pthread_create);
    LOAD_FUNC(pthread_mutex_lock);
    LOAD_FUNC(pthread_barrier_wait);
    LOAD_FUNC(pthread_rwlock_rdlock);
    LOAD_FUNC(pthread_rwlock_wrlock);

    LOAD_FUNC(pthread_cond_init);

    EXPECT_EXIT(!set_numa_affinity(numa_node));

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
    thread_info_t *_thread_info_iter;
    const char *ofile, *e;

    ofile = NULL;
    if ((e = getenv("PREPROF_FILE")))
        ofile = e;

    /* Initialize log header */
    memset(&header, 0, sizeof header);
    header.version = LOG_VERSION_CURRENT;
    header.num_processes = nthreads;

    EXPECT_EXIT(log_create(&log, &header, ofile) == LOG_ERROR_OK);

#ifdef AGGREGATE
    memset(&event, 0, sizeof(event));
    THREAD_INFO_VECT_FOREACH(_thread_info_iter)
        event.pmc[event.num_processes++].tsc = _thread_info_iter->mutex_spin;
    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);

    memset(&event, 0, sizeof(event));
    THREAD_INFO_VECT_FOREACH(_thread_info_iter)
        event.pmc[event.num_processes++].tsc = _thread_info_iter->rwlock_spin;
    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);

    memset(&event, 0, sizeof(event));
    THREAD_INFO_VECT_FOREACH(_thread_info_iter)
        event.pmc[event.num_processes++].tsc = _thread_info_iter->barrier_spin;
    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);

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
    THREAD_INFO_VECT_FOREACH(_thread_info_iter) {
        if (max_tsc_end < _thread_info_iter->tsc_end)
            max_tsc_end = _thread_info_iter->tsc_end;
        if (min_tsc_start > _thread_info_iter->tsc_start)
            min_tsc_start = _thread_info_iter->tsc_start;
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


    /* XXX Figure out a way to dump this in log file */
    for (int i = 0; i < nthreads; i++) {
        struct thread_info_s *t = &thread_info_vect[i];

        printf("lockprof: thread %d: cycles: %lu\n",
                i, t->tsc_end - t->tsc_start);
    }
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    if (!opt_null_sync)
        res = real_pthread_mutex_lock(mutex);
    else
        res = 0;
    tsc_diff = read_tsc_p() - tsc1;

#ifdef AGGREGATE
    thread_info->mutex_spin += tsc_diff;
#else
    g_array_append_val(thread_info->timestamp, tsc_diff);
#endif /* AGGREGATE */

    return res;
}

int
pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    if (!opt_null_sync)
        res = real_pthread_rwlock_rdlock(rwlock);
    else
        res = 0;
    tsc_diff = read_tsc_p() - tsc1;

#ifdef AGGREGATE
    thread_info->rwlock_spin += tsc_diff;
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
    if (!opt_null_sync)
        res = real_pthread_rwlock_wrlock(rwlock);
    else
        res = 0;
    tsc_diff = read_tsc_p() - tsc1;

#ifdef AGGREGATE
    thread_info->rwlock_spin += tsc_diff;
#else
    g_array_append_val(thread_info->timestamp, tsc_diff);
#endif /* AGGREGATE */

    return res;
}

int
pthread_barrier_wait(pthread_barrier_t *barrier)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    if (!opt_null_sync)
        res = real_pthread_barrier_wait(barrier);
    else
        res = 0; /* XXX Should return PTHREAD_BARRIER_SERIAL_THREAD for one thread */
    tsc_diff = read_tsc_p() - tsc1;

#ifdef AGGREGATE
    thread_info->barrier_spin += tsc_diff;
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

static void *
_start_routine(void *arg)
{
    void *res;

    thread_info = arg;

    EXPECT_RET(!set_core_affinity(thread_info->core), NULL);

    thread_info->tsc_start = read_tsc_p();
    res = (*thread_info->start_routine)(thread_info->arg);
    thread_info->tsc_end = read_tsc_p();

    return res;
}

int
pthread_create(pthread_t *newthread, const pthread_attr_t *attr,
	       void *(*start_routine) (void *), void *arg)
{
    struct thread_info_s *_thread_info; 

    _thread_info = &thread_info_vect[nthreads];
    _thread_info->start_routine = start_routine;
    _thread_info->arg = arg;
    _thread_info->core = thread_to_core_map[nthreads % THREAD_TO_CORE_MAP_SIZE];

    ++nthreads;

    return real_pthread_create(newthread, attr, _start_routine, _thread_info);
}


