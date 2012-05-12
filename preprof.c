#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

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
};

static int (*real_pthread_create)(pthread_t *newthread,
				  const pthread_attr_t *attr,
				  void *(*start_routine) (void *),
				  void *arg) = NULL;
static int (*real_pthread_join)(pthread_t thread, void **retval) = NULL;

static struct perfctr_cpu_control perf_control;

static bool  opt_one_thread = false;
static char *opt_cmd;

static void setup(void) __attribute ((constructor));
static void shutdown(void) __attribute ((destructor));

static const char *
get_prname(void)
{
	static char prname[17];
	int r;

	r = prctl(PR_GET_NAME, prname);
	assert(r == 0);

	prname[16] = 0;

	return prname;
}

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
        unsigned int offcore_rsp0;
        unsigned int ievent;
        unsigned long icount;

	load_functions();

	if (!(e = getenv("PREPROF_CMD")))
		fprintf(stderr, "preprof: WARNING: Failed to parse"
			" $PREPROF_CMD.\n");
	else {
		opt_cmd = e;
		printf("Executing cmd \"%s\"\n", opt_cmd);
	}

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

	perfctr_control_init(&perf_control, &event_vect, offcore_rsp0, ievent, icount);

	fprintf(stderr, "preprof: successfully initialized"
		" for process %s (PID: %lu).\n", get_prname(),
		(unsigned long) getpid());
}

static void
shutdown(void)
{
}

static void *
wrapped_start_routine(void *arg)
{
	void *real_ret = NULL;
	struct thread_info *thread = arg;

	if (thread->run_thread) {
                int perf_fd;
                struct perfctr_sum_ctrs ctrs;

	        /* Start the performance counters */
	        EXPECT_RET((perf_fd = perfctr_open()) != -1, NULL);
	        EXPECT_RET(!perfctr_init(perf_fd, &perf_control), NULL);

		real_ret = (*thread->routine)(thread->arg);

                EXPECT_RET(!_vperfctr_read_sum(perf_fd, &ctrs), NULL);
        }

	return real_ret;
}

int
pthread_create(pthread_t *newthread, const pthread_attr_t *attr,
	       void *(*start_routine) (void *), void *arg)
{
	struct thread_info *thread;
	EXPECT((thread = malloc(sizeof*thread)) != NULL); /* LEAKS */
        static int first = 1;

	thread->routine = start_routine;
	thread->arg = arg;
	thread->run_thread = true;
		
        if (!first && opt_one_thread)
		thread->run_thread = false;
        first = 0;

	return real_pthread_create(newthread, attr, wrapped_start_routine, thread);
}

int
pthread_join(pthread_t thread, void **retval)
{
	return real_pthread_join(thread, retval);
}
