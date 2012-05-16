#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ptrace.h>
#include <libperfctr.h>

#include "pirate.h"
#include "utils.h"
#include "expect.h"

#define MAX_PID PIRATE_MAX_PROC

#define STRIDE 64

#define ER(x) EXPECT_RET(x, -1)
#define EE(x) EXPECT_EXIT(x)

typedef struct {
    pid_t pid;
    int perf_fd;

    struct perfctr_sum_ctrs ctrs_begin;
    struct perfctr_sum_ctrs ctrs_end;
} process_ctx_t;

static pirate_conf_t conf;

static process_ctx_t process_ctx[MAX_PID];
#define PCTX(pid)     (&process_ctx[pid])

static volatile char *mem;

static int _pirate_cont(int tid);
static int _pirate_stop(int tid);

int
pirate_init(pirate_conf_t *conf_)
{
    conf = *conf_;
    
    ER(conf.processes < MAX_PID);
    ER((mem = utils_calloc(1, conf.footprint)) != NULL);
    return 0;
}

int
pirate_fini(void)
{
    utils_free((void *)mem, 1, conf.footprint);
    return 0;
}

int
pirate_warm(void)
{
    size_t i;

    for (i =  0; i <  conf.footprint; i += STRIDE)
         (void)mem[i];

    return 0;
}


static void __attribute__ ((noreturn))
_pirate(int tid)
{
    register size_t i;
    register size_t size = conf.footprint;
    register size_t stride =  conf.processes * STRIDE;
    register size_t offset = tid * STRIDE;
    register volatile char *m = mem;

    do {
        for (i = offset; i < size; i += stride) {
            (void)m[i];
        }
    } while (1);
}


#define FORALL_TID_FUNCDEF(name)                        \
    int name(void)                                      \
    {                                                   \
        for (int tid = 0; tid < conf.processes; tid++)  \
            ER(!_ ## name(tid));                        \
        return 0;                                       \
    }

static int
_pirate_launch(int tid)
{
    int pid, fd;
    int sockets[2];
    ER(!socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets));
    ER((pid = fork()) != -1);
    if (!pid) {
        //ER(setpgid(0, getppid()));
        EE(!utils_setaffinity(conf.cores[tid]));
        EE(!utils_setnumanode(conf.node));

        EE(fd = perfctr_open());
        EE(!utils_send_fd(sockets[1], fd));
        
        close(sockets[0]);
        close(sockets[1]);
        
        EE(!ptrace(PTRACE_TRACEME, 0, NULL, NULL));
        _pirate(tid);
        assert(0);
    } else {
        ER(!utils_recv_fd(sockets[0], &fd));
        close(sockets[0]);
        close(sockets[1]);
        PCTX(tid)->pid = pid;
        PCTX(tid)->perf_fd = fd;
        ER(!_pirate_stop(tid));
        ER(!perfctr_init(fd, &conf.cpu_control));
    }
    return 0;
}

FORALL_TID_FUNCDEF(pirate_launch)

static int
_pirate_cont(int tid)
{
    ER(!perfctr_read(PCTX(tid)->perf_fd, &PCTX(tid)->ctrs_begin));
    ER(!ptrace(PTRACE_CONT, PCTX(tid)->pid, NULL, NULL));
    return 0;
}

FORALL_TID_FUNCDEF(pirate_cont)

static int
_pirate_stop(int tid)
{
    int status;
    ER(!kill(PCTX(tid)->pid, SIGSTOP));
    
    ER(waitpid(PCTX(tid)->pid, &status, 0) != -1);
    assert(!WIFEXITED(status));
    assert(WIFSTOPPED(status));
    switch (WSTOPSIG(status)) {
        case SIGSTOP:
            break;
        default:
            fprintf(stderr, "Error: Pirate recived signal: %d\n", WSTOPSIG(status));
            return -1;
    }

    ER(!perfctr_read(PCTX(tid)->perf_fd, &PCTX(tid)->ctrs_end));
    return 0;
}

FORALL_TID_FUNCDEF(pirate_stop)

static int
_pirate_kill(int tid)
{
    ER(!ptrace(PTRACE_KILL, PCTX(tid)->pid, NULL, NULL));
    return 0;
}

FORALL_TID_FUNCDEF(pirate_kill)


static int
_pirate_ctrs_read(int tid, struct perfctr_sum_ctrs *ctrs)
{
    unsigned int i;

    struct perfctr_sum_ctrs *begin = &PCTX(tid)->ctrs_begin;
    struct perfctr_sum_ctrs *end = &PCTX(tid)->ctrs_end;

    for (i = 0; i < conf.cpu_control.nractrs; i++)
        ctrs->pmc[i] = end->pmc[i] - begin->pmc[i];
    ctrs->tsc = end->tsc - begin->tsc;
    return 0;
}

int
pirate_ctrs_read(struct perfctr_sum_ctrs *ctrs)
{
    int tid;

    for (tid = 0; tid < conf.processes; tid++)
        ER(!_pirate_ctrs_read(tid, &ctrs[tid]));
    return 0;
}

