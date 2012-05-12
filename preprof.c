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
#include "log.h"
#include "process.h"
#include "expect.h"
#include "external/cclib/cclib.h"

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

static volatile int nthreads = 0;

static struct perfctr_cpu_control perf_control;

static bool  opt_one_thread = false;
static char *opt_log_filename;
static char *opt_cmd;

log_t plog;
process_vect_t proc_vect;

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

static int
_log_header_init(process_t *p, log_header_process_t *h)
{
	utils_md5hash_t md5hash;
	int i = 0;
	char **iter;

	h->core = p->core;
	h->node = p->node;

	VECT_FOREACH(&p->argv, iter) {
		if (i > LOG_MAX_ARGC || *iter == NULL)
			break;
		strncpy(h->argv[i++], *iter, LOG_MAX_ARG_LEN);
	}
	h->argc = i;

	memcpy(h->allowed_colors, p->allowed_colors, CC_MASK_LEN);

	EXPECT(!utils_md5(VECT_ELEM(&p->argv, 0), md5hash));
	memcpy(&h->md5hash, md5hash, sizeof(md5hash));

	h->pmc_map.counters = perf_control.nractrs + perf_control.nrictrs;
	h->pmc_map.offcore_rsp0 = perf_control.nhlm.offcore_rsp[0];
	h->pmc_map.ireset = perf_control.ireset[h->pmc_map.counters];
	for (i = 0; i < (int)h->pmc_map.counters && i < LOG_MAX_CTRS; i++)
		h->pmc_map.eventsel_map[i] = perf_control.evntsel[i];

	return 0;
}

static int
log_header_init(process_vect_t *pv, log_header_t *h)
{
	int i = 0;
	process_t *iter;

	memset(h, 0, sizeof *h);
	h->version = LOG_VERSION_CURRENT;

	VECT_FOREACH(pv, iter)
		EXPECT(!_log_header_init(iter, &h->processes[i++]));
	h->num_processes = i;

	return 0;
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

	e = opt_log_filename;
	if ((e = getenv("PREPROF_FILE")))
		opt_log_filename = e;

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

	VECT_INIT(&proc_vect);
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
	process_t _proc, *proc;
	char *e, *cmd, *cmd_r;
	struct thread_info *thread = arg;
	//pid_t tid = (pid_t)syscall(SYS_gettid);

	process_init(&_proc);
	VECT_APPEND(&proc_vect, _proc);
	proc = &VECT_LAST(&proc_vect);

	/* Save command line for the header of the log file */
	cmd = strdup(opt_cmd);
	VECT_INIT(&proc->argv);
	do {
		e = strtok_r(cmd, " ", &cmd_r);
		VECT_APPEND(&proc->argv, e);
		cmd = NULL;
	} while (e != NULL);
	free(cmd); /* strdup returns allocated char[] */

	/* Start the performance counters */
	EXPECT_RET((proc->perf_fd = perfctr_open()) != -1, NULL);
	EXPECT_RET(!perfctr_init(proc->perf_fd, &perf_control), NULL);

	if (thread->run_thread)
		real_ret = (*thread->routine)(thread->arg);

	proc->state = EXITED;

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

	__sync_fetch_and_add(&nthreads, 1);

	return real_pthread_create(newthread, attr, wrapped_start_routine, thread);
}

int
pthread_join(pthread_t thread, void **retval)
{
	if (!__sync_sub_and_fetch(&nthreads, 1)) {
		log_event_t event;
		log_header_t header;

		EXPECT_EXIT(!log_header_init(&proc_vect, &header));
		EXPECT_EXIT(log_create(&plog, &header, opt_log_filename) == LOG_ERROR_OK);

		EXPECT_EXIT(!process_read_counter_all(&proc_vect, &event));
		EXPECT_EXIT(log_write_event(&plog, &event) == LOG_ERROR_OK);
		EXPECT_EXIT(log_close(&plog) == LOG_ERROR_OK);
	}
	return real_pthread_join(thread, retval);
}
