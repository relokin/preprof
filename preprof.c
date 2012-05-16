#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <dlfcn.h>

#include "log.h"
#include "pirate.h"
#include "utils.h"
#include "expect.h"

#define PERFCTR_CNT 10

#if !defined (__linux__) || !defined(__GLIBC__)
#error "This stuff only works on Linux!"
#endif

#define LIKELY(x) (__builtin_expect(!!(x),1))
#define UNLIKELY(x) (__builtin_expect(!!(x),0))

struct thread_info {
    void *(*routine) (void *);
    void *arg;
    int core;
    bool run_thread;
    struct perfctr_sum_ctrs perf_ctrs;
};

/* Global variables */
#define MAX_PROFILED_THREADS 128
static struct thread_info thread_info_vect[MAX_PROFILED_THREADS];

static volatile int created_threads;
static volatile int running_threads;

static struct perfctr_cpu_control thread_perf_control;
static struct perfctr_cpu_control pirate_perf_control;

static struct perfctr_sum_ctrs pirate_ctrs[PIRATE_MAX_PROC];

static bool  opt_one_thread = false;
static bool  opt_pirate = false;
static int   opt_pirate_wset;
static char *opt_file;

#define AFFINITY_MAP_SIZE 4
static int affinity_map[AFFINITY_MAP_SIZE] = {1, 3, 5, 7};

static int (*real_pthread_create)(pthread_t *newthread,
        const pthread_attr_t *attr,
        void *(*start_routine) (void *),
        void *arg) = NULL;
static int (*real_pthread_join)(pthread_t thread, void **retval) = NULL;

/* Function declarations */
static void setup(void) __attribute ((constructor));
static void shutdown(void) __attribute ((destructor));


#define LOAD_FUNC(name) do {                            \
    *(void**) (&real_##name) = dlsym(RTLD_NEXT, #name); \
    assert(real_##name);                                \
} while (false)                                         \

static void
load_functions(void)
{
    LOAD_FUNC(pthread_create);
    LOAD_FUNC(pthread_join);
}

static void
setup(void)
{
    int i;
    char *e;
    event_vect_t event_vect = VECT_NULL;
    unsigned int offcore_rsp0 = 0;
    unsigned int ievent = 0;
    unsigned long icount = 0;
    char *prname = NULL;

    load_functions();

    /* Parse parameters */
    if ((e = getenv("PREPROF_RUN_ONE_THREAD")))
        opt_one_thread = true;

    if ((e = getenv("PREPROF_PIRATE_WSET"))) {
        opt_pirate = true;
        opt_pirate_wset = atoi(e);
        opt_one_thread = true;
    }

    for (i = 0; i < PERFCTR_CNT; i++) {
        char temp[100];
        snprintf(temp, 100, "PREPROF_EVENT%d", i);
        if ((e = getenv(temp))) {
            VECT_APPEND(&event_vect, strtoul(e, NULL, 16));
            printf("PerfCtr event%d=%lx\n", i, strtoul(e, NULL, 16));
        }
    }

    if ((e = getenv("PREPROF_OFFCORE_RSP0")))
        offcore_rsp0 = strtoul(e, NULL, 16);

    if ((e = getenv("PREPROF_IEVENT")))
        ievent = strtoul(e, NULL, 16);

    if ((e = getenv("PREPROF_ICOUNT")))
        icount = strtoul(e, NULL, 16);

    if ((e = getenv("PREPROF_FILE")))
        opt_file = e;

    if ((e = getenv("_")))
        prname = e;

    perfctr_control_init(&thread_perf_control, &event_vect, offcore_rsp0, ievent, icount);

    if (opt_pirate) {
        pirate_conf_t pirate_conf;

        /* Hardcoded pirate performance counter */
        pirate_perf_control.tsc_on = 1;

        pirate_perf_control.pmc_map[0] = 0;
        pirate_perf_control.evntsel[0] = 0x41010b; // Loads

        pirate_perf_control.pmc_map[1] = 1;
        pirate_perf_control.evntsel[1] = 0x41020b; // Stores

        pirate_perf_control.pmc_map[2] = 2;
        pirate_perf_control.evntsel[2] = 0x4101b7;
        pirate_perf_control.nhlm.offcore_rsp[0] = 0xf077; // Fetches

        pirate_perf_control.nractrs = 3;

        /* Initialize pirate */
        pirate_conf.processes = 1;
        pirate_conf.cores[0] = 7;
        pirate_conf.footprint = opt_pirate_wset;
        pirate_conf.cpu_control = pirate_perf_control;

        EXPECT_EXIT(!pirate_init(&pirate_conf));
        EXPECT_EXIT(!pirate_launch());
    }

    EXPECT_EXIT(!utils_setaffinity(affinity_map[0]));

    fprintf(stderr, "preprof: successfully initialized %s (PID: %lu).\n",
            prname ? prname : "", (unsigned long) getpid());
}

static void
shutdown(void)
{
    int i;
    log_t log;
    log_header_t header;
    log_event_t event;

    /* Finilizing pirate */
    if (opt_pirate) {
        EXPECT_EXIT(!pirate_kill());
        EXPECT_EXIT(!pirate_fini());
    }

    /* Initialize log header */
    header.version = LOG_VERSION_CURRENT;
    for (i = 0; i < created_threads; i++)
        log_header_append(&header, &thread_perf_control);
    for (i = 0; i < 1; i++)
        log_header_append(&header, &pirate_perf_control);

    /* Initialize log event */
    memset(&event, 0, sizeof(event));
    for (i = 0; i < created_threads; i++) {
        struct thread_info *thread = &thread_info_vect[i];
        log_event_append(&event, &thread_perf_control, &thread->perf_ctrs);
    }

    if (opt_pirate) {
        for (i = 0; i < 1; i++)
            log_event_append(&event, &pirate_perf_control, &pirate_ctrs[i]);
    }

    /* Write event to log file */
    EXPECT_EXIT(log_create(&log, &header, opt_file) == LOG_ERROR_OK);
    EXPECT_EXIT(log_write_event(&log, &event) == LOG_ERROR_OK);
    EXPECT_EXIT(log_close(&log) == LOG_ERROR_OK);
}

static void *
wrapped_start_routine(void *arg)
{
    void *real_ret = NULL;
    struct thread_info *thread = arg;

    if (thread->run_thread) {
        int perf_fd;
        struct perfctr_sum_ctrs ctrs;
    
        EXPECT_RET(!utils_setaffinity_pthread(pthread_self(), thread->core), NULL);

        /* Start performance counters */
        EXPECT_RET((perf_fd = perfctr_open()) != -1, NULL);
        EXPECT_RET(!perfctr_init(perf_fd, &thread_perf_control), NULL);

        real_ret = (*thread->routine)(thread->arg);

        /* Read performance counters */
        EXPECT_RET(!_vperfctr_read_sum(perf_fd, &ctrs), NULL);
        thread->perf_ctrs = ctrs;
    }

    return real_ret;
}

int
pthread_create(pthread_t *newthread, const pthread_attr_t *attr,
        void *(*start_routine) (void *), void *arg)
{
    struct thread_info *thread;
    static int first = 1;

    /* XXX test that created_threads is less than MAX_PROFILED_THREADS */
    thread = &thread_info_vect[created_threads];
    thread->routine = start_routine;
    thread->arg = arg;
    thread->core = affinity_map[created_threads & (AFFINITY_MAP_SIZE - 1)];
    thread->run_thread = true;
    ++created_threads;

    if (first) {
        if (opt_pirate) {
            EXPECT(!pirate_warm());
            EXPECT(!pirate_cont());
        }
    } else {
        if (opt_one_thread) {
            thread->run_thread = false;
        }
    }
    first = 0;

    __sync_fetch_and_add(&running_threads, 1);

    return real_pthread_create(newthread, attr, wrapped_start_routine, thread); 
}

int
pthread_join(pthread_t thread, void **retval)
{
    int rc;

    rc = real_pthread_join(thread, retval);

    if (!__sync_sub_and_fetch(&running_threads, 1)) {
        if (opt_pirate) {
            EXPECT(!pirate_stop());
            EXPECT(!pirate_ctrs_read(pirate_ctrs));
        }
    }

    return rc;
}
