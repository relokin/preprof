#include "config.h"

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

#include "utils.h"
#include "log.h"
#include "process.h"
#include "expect.h"
#include "external/cclib/cclib.h"

#if !defined (__linux__) || !defined(__GLIBC__)
#error "This stuff only works on Linux!"
#endif


#if defined(__i386__) || defined(__x86_64__)
#define DEBUG_TRAP __asm__("int $3")
#else
#define DEBUG_TRAP raise(SIGTRAP)
#endif

#define LIKELY(x) (__builtin_expect(!!(x),1))
#define UNLIKELY(x) (__builtin_expect(!!(x),0))

static int (*real_pthread_create)(pthread_t *newthread,
				  const pthread_attr_t *attr,
				  void *(*start_routine) (void *),
				  void *arg) = NULL;
static void (*real_exit)(int status) __attribute__((noreturn)) = NULL;
static void (*real__exit)(int status) __attribute__((noreturn)) = NULL;
static void (*real__Exit)(int status) __attribute__((noreturn)) = NULL;


static __thread bool recursive = false;

static volatile bool initialized = false;
static volatile bool threads_existing = false;
char *log_filename;

static void setup(void) __attribute ((constructor));
static void shutdown(void) __attribute ((destructor));

static const char *get_prname(void) {
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

static void load_functions(void)
{
        static volatile bool loaded = false;

        if (LIKELY(loaded))
                return;

        recursive = true;

        /* If someone uses a shared library constructor that is called
         * before ours we might not be initialized yet when the first
         * lock related operation is executed. To deal with this we'll
         * simply call the original implementation and do nothing
         * else, but for that we do need the original function
         * pointers. */

        LOAD_FUNC(pthread_create);

        LOAD_FUNC(exit);
        LOAD_FUNC(_exit);
        LOAD_FUNC(_Exit);

        loaded = true;
        recursive = false;
}

static void setup(void)
{
        char *e;

        load_functions();

        if (LIKELY(initialized))
                return;

        if (!dlsym(NULL, "main"))
                fprintf(stderr, "mutrace: Application appears to be compiled"
			" without -rdynamic. It might be a\n"
                        "mutrace: good idea to recompile with -rdynamic enabled"
			" since this produces more\n"
                        "mutrace: useful stack traces.\n\n");

	e = log_filename;
        if (!(e = getenv("PREPROF_FILE")))
                fprintf(stderr, "preprof: WARNING: Failed to parse"
			" $PREPROF_FILE.\n");
	else
		log_filename = e;

        initialized = true;

        fprintf(stderr, "mutrace: "PACKAGE_VERSION" successfully initialized"
		" for process %s (PID: %lu).\n", get_prname(),
		(unsigned long) getpid());
}

static  __thread void *(*real_start_routine)(void *) = NULL;

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
        memcpy(h->argv[i++], *iter, LOG_MAX_ARG_LEN);
    }
    h->argc = i;

    memcpy(h->allowed_colors, p->allowed_colors, CC_MASK_LEN);

    EXPECT(!utils_md5(VECT_ELEM(&p->argv, 0), md5hash));
    memcpy(&h->md5hash, md5hash, sizeof(md5hash));

    h->pmc_map.counters = p->cpu_control.nractrs + p->cpu_control.nrictrs;
    h->pmc_map.offcore_rsp0 = p->cpu_control.nhlm.offcore_rsp[0];
    h->pmc_map.ireset = p->cpu_control.ireset[h->pmc_map.counters];
    for (i = 0; i < (int)h->pmc_map.counters && i < LOG_MAX_CTRS; i++)
        h->pmc_map.eventsel_map[i] = p->cpu_control.evntsel[i];

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

static void *wrapped_start_routine(void *arg)
{
	int fd;
	void *real_ret;
	log_t log;
	log_header_t header;
	log_event_t event;
	struct perfctr_cpu_control cpu_control;
	process_vect_t proc_vect;

	printf("PID of this process: %d\n", getpid());
	printf("The ID of this thread is: %u\n", (unsigned int)pthread_self());
	EXPECT(!log_header_init(&proc_vect, &header));
	EXPECT(log_create(&log, &header, log_filename) == LOG_ERROR_OK);

	EXPECT((fd = perfctr_open()) != -1);
        /* Start the performance counters */
        EXPECT(!perfctr_init(fd, &cpu_control));

	real_ret = real_start_routine(arg);

	EXPECT(!process_read_counter_all(&proc_vect, &event));
        EXPECT(log_write_event(&log, &event) == LOG_ERROR_OK);

	return real_ret;
}

int pthread_create(pthread_t *newthread,
                   const pthread_attr_t *attr,
                   void *(*start_routine) (void *),
                   void *arg)
{

        load_functions();

        if (UNLIKELY(!threads_existing)) {
                threads_existing = true;
                setup();
        }

	real_start_routine = start_routine;
        return real_pthread_create(newthread, attr, wrapped_start_routine, arg);
}


static void shutdown(void)
{
//        show_summary();
}

void exit(int status)
{
//        show_summary();
        real_exit(status);
}

void _exit(int status)
{
//        show_summary();
        real_exit(status);
}

void _Exit(int status)
{
//        show_summary();
        real__Exit(status);
}
