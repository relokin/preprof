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

static int (*real_pthread_create)(pthread_t *, const pthread_attr_t *,
				  void *(*) (void *), void *);

static int (*real_pthread_mutex_lock)(pthread_mutex_t *);
static int (*real_pthread_barrier_wait)(pthread_barrier_t *);
static int (*real_pthread_rwlock_rdlock)(pthread_rwlock_t *);
static int (*real_pthread_rwlock_wrlock)(pthread_rwlock_t *);

/* No need to do anything with them now */
// static int (*real_pthread_mutex_trylock)(pthread_mutex_t *);
// static int (*real_pthread_mutex_unlock)(pthread_mutex_t *);

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

struct thread_info_s {
    void *(*start_routine) (void *);
    void *arg;
    bool used;
    GArray *timestamp;
    uint64_t tsc_end;
};

/* Global variables */
#define MAX_PROFILED_THREADS 128
static struct thread_info_s thread_info_vect[MAX_PROFILED_THREADS];
static __thread struct thread_info_s *thread_info;

#define LOAD_FUNC(name) do {                            \
    *(void**) (&real_##name) = dlsym(RTLD_NEXT, #name); \
    assert(real_##name);                                \
} while (0)


static void
init(void)
{
    LOAD_FUNC(pthread_create);

    LOAD_FUNC(pthread_mutex_lock);
    LOAD_FUNC(pthread_barrier_wait);
    LOAD_FUNC(pthread_rwlock_rdlock);
    LOAD_FUNC(pthread_rwlock_wrlock);

    LOAD_FUNC(pthread_cond_init);

    for (int i = 0; i < MAX_PROFILED_THREADS; i++)
	thread_info_vect[i].timestamp = g_array_new(FALSE, FALSE,
						    sizeof(uint64_t));
}

static void
fini(void)
{
    log_t log;
    log_header_t header;
    int nthreads = 0;
    const char *ofile, *e;

    ofile = "foo.log";
    if ((e = getenv("PREPROF_FILE")))
        ofile = e;

    /* Initialize log header */
    header.version = LOG_VERSION_CURRENT;

    for (int i = 0; i < MAX_PROFILED_THREADS; i++)
	if (thread_info_vect[i].used) {
	    nthreads++;

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
    int len = thread_info_vect[0].timestamp->len;
    for (int j = 0; j < len; j++) {
	for (int i = 0; i < nthreads; i++) {
	    GArray *timestamp = thread_info_vect[i].timestamp;
	    log_event_t event;

	    /* Initialize log event */
	    memset(&event, 0, sizeof(event));
	    event.pmc[event.num_processes++].tsc =
		g_array_index(timestamp, uint64_t, j);
	    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);
	}
    }

    uint64_t max_tsc_end = UINT64_C(0);
    for (int i = 0; i < nthreads; i++)
	if (max_tsc_end < thread_info_vect[i].tsc_end)
	    max_tsc_end = thread_info_vect[i].tsc_end;

    for (int i = 0; i < nthreads; i++) {
	    log_event_t event;

	    /* Initialize log event */
	    memset(&event, 0, sizeof(event));
	    event.pmc[event.num_processes++].tsc =
		max_tsc_end - thread_info_vect[i].tsc_end;
	    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);
    }

    EXPECT_EXIT(log_close(&log) == LOG_ERROR_OK);

    for (int i = 0; i < MAX_PROFILED_THREADS; i++)
	g_array_free(thread_info_vect[i].timestamp, FALSE);

}

static void *
_start_routine(void *arg)
{
    void *res = NULL;

    thread_info = arg;

    res = (*thread_info->start_routine)(thread_info->arg);
    thread_info->tsc_end = read_tsc_p();

    return res;
}

int
pthread_create(pthread_t *newthread, const pthread_attr_t *attr,
	       void *(*start_routine) (void *), void *arg)
{
    static int tid = 0;
    struct thread_info_s *_thread_info = &thread_info_vect[tid++];

    _thread_info->start_routine = start_routine;
    _thread_info->arg = arg;
    _thread_info->used = true;

    return real_pthread_create(newthread, attr, _start_routine, arg);
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    res = real_pthread_mutex_lock(mutex);
    tsc_diff = read_tsc_p() - tsc1;
    g_array_append_val(thread_info->timestamp, tsc_diff);

    return res;
}

int
pthread_barrier_wait(pthread_barrier_t *barrier)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    res = real_pthread_barrier_wait(barrier);
    tsc_diff = read_tsc_p() - tsc1;
    g_array_append_val(thread_info->timestamp, tsc_diff);

    return res;
}

int
pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    int res;
    uint64_t tsc1, tsc_diff;

    tsc1 = read_tsc_p();
    res = real_pthread_rwlock_rdlock(rwlock);
    tsc_diff = read_tsc_p() - tsc1;
    g_array_append_val(thread_info->timestamp, tsc_diff);

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
    g_array_append_val(thread_info->timestamp, tsc_diff);

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
