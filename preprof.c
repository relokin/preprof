#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <dlfcn.h>

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
	bool run_thread;
        struct perfctr_sum_ctrs perf_ctrs;
};

/* Global variables */
static VECT(struct thread_info) thread_info_vect = VECT_NULL;

static volatile int nthreads = 0;

static struct perfctr_cpu_control perf_control;

static pirate_conf_t pirate_conf;
static struct perfctr_sum_ctrs pirate_ctrs[PIRATE_MAX_PROC];

static bool opt_one_thread = false;
static bool opt_pirate = false;
static int  opt_pirate_procs = 1;

static int (*real_pthread_create)(pthread_t *newthread,
				  const pthread_attr_t *attr,
				  void *(*start_routine) (void *),
				  void *arg) = NULL;
static int (*real_pthread_join)(pthread_t thread, void **retval) = NULL;

/* Function declarations */
static void setup(void) __attribute ((constructor));
static void shutdown(void) __attribute ((destructor));


#define LOAD_FUNC(name)							\
	do {								\
		*(void**) (&real_##name) = dlsym(RTLD_NEXT, #name);	\
		assert(real_##name);					\
	} while (false)

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

	if ((e = getenv("PREPROF_RUN_ONE_THREAD")))
		opt_one_thread = true;

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

        if ((e = getenv("_")))
                prname = e;

	perfctr_control_init(&perf_control, &event_vect, offcore_rsp0, ievent, icount);

        /* Initialize pirate */
        if (opt_pirate) {
                /* XXX Hardcoded for testing purposes */
                pirate_conf.processes = opt_pirate_procs;
                pirate_conf.footprint = 4 * (1 << 20);

                /* Hardcoded performance counter */
                pirate_conf.cpu_control.tsc_on = 1;

                pirate_conf.cpu_control.pmc_map[0] = 0;
                pirate_conf.cpu_control.evntsel[0] = 0x41010b; // Loads

                pirate_conf.cpu_control.pmc_map[1] = 1;
                pirate_conf.cpu_control.evntsel[1] = 0x41020b; // Stores

#if 0
                /* XXX error: 'struct <anonymous>' has no member named 'offcore_rsp0'*/
                pirate_conf.cpu_control.pmc_map[2] = 2;
                pirate_conf.cpu_control.evntsel[2] = 0x4101b7;
                pirate_conf.cpu_control.nhlm.offcore_rsp0[0] = 0xf077; // Fetches

                pirate_conf.cpu_control.nractrs = 3;
#else
                pirate_conf.cpu_control.nractrs = 2;
#endif

                EXPECT_EXIT(!pirate_init(&pirate_conf));
                EXPECT_EXIT(!pirate_launch());
        }

	fprintf(stderr, "preprof: successfully initialized %s (PID: %lu).\n",
                prname ? prname : "", (unsigned long) getpid());
}

/* XXX Log to stdout for now */
static void
log_perf_ctrs(struct perfctr_cpu_control *ctrl, struct perfctr_sum_ctrs *ctrs)
{
        unsigned int i;

        printf("tsc: %llu\n", ctrs->tsc);
        for (i = 0; i < ctrl->nractrs; i++)
                printf("%#x: %llu\n", ctrl->evntsel[i], ctrs->pmc[i]);
        printf("\n");
}

static void
shutdown(void)
{
        printf("\nTarget counters:\n");

        struct thread_info *iter;
        VECT_FOREACH(&thread_info_vect, iter) {
                if (iter->run_thread)
                log_perf_ctrs(&perf_control, &iter->perf_ctrs);
        }

        if (opt_pirate) {
                int i;

                printf("Pirate counters:\n");
                for (i = 0; i < opt_pirate_procs; i++) 
                        log_perf_ctrs(&pirate_conf.cpu_control, &pirate_ctrs[i]);

                /* Finilizing pirate */
                EXPECT_EXIT(!pirate_kill());
                EXPECT_EXIT(!pirate_fini());
        }
}

static void *
wrapped_start_routine(void *arg)
{
	void *real_ret = NULL;
	struct thread_info *thread = arg;

	if (thread->run_thread) {
                int perf_fd;
                struct perfctr_sum_ctrs ctrs;

	        /* Start performance counters */
	        EXPECT_RET((perf_fd = perfctr_open()) != -1, NULL);
	        EXPECT_RET(!perfctr_init(perf_fd, &perf_control), NULL);

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
	struct thread_info thread;
        static int first = 1;

        memset(&thread, 0, sizeof thread);
	thread.routine = start_routine;
	thread.arg = arg;
	thread.run_thread = true;
		
        if (first) {
                if (opt_pirate) {
                        EXPECT(!pirate_warm());
                        EXPECT(!pirate_cont());
                }
        } else {
                if (opt_one_thread)
		        thread.run_thread = false;
        }
        first = 0;

        __sync_fetch_and_add(&nthreads, 1);
        VECT_APPEND(&thread_info_vect, thread);

	return real_pthread_create(newthread, attr, wrapped_start_routine, &VECT_LAST(&thread_info_vect));
}

int
pthread_join(pthread_t thread, void **retval)
{
        int rc;

	rc = real_pthread_join(thread, retval);

        if (!__sync_sub_and_fetch(&nthreads, 1)) {
                if (opt_pirate) {
                        EXPECT(!pirate_stop());
                        EXPECT(!pirate_ctrs_read(pirate_ctrs));
                }
        }

        return rc;
}
