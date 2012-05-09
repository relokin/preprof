#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/socket.h>

#include "process.h"
#include "expect.h"
#include "utils.h"
#include "external/cclib/cclib.h"

int
process_init(process_t *p)
{
    memset(p, 0, sizeof *p);

    p->core = -1;
    p->node = -1;
    VECT_INIT(&p->argv);
    return 0;
}

int
process_fini(process_t *p)
{
    VECT_FINI(&p->argv);
    return 0;
}

int
process_launch(process_t *p)
{
    pid_t pid;
    int sockets[2];

    EXPECT(!socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets));

    pid = fork();
    EXPECT(pid >= 0);

    if (!pid) {
        int fd;

        if (p->core != -1)
            EXPECT(!utils_setaffinity(p->core));
        if (p->node != -1)
            EXPECT(!utils_setnumanode(p->node));

        if (p->stdin)
            EXPECT(freopen(p->stdin, "r", stdin) != NULL);
        if (p->stdout)
            EXPECT(freopen(p->stdout, "w+", stdin) != NULL);
        if (p->stderr)
            EXPECT(freopen(p->stderr, "w+", stdin) != NULL);

        if (strlen(p->allowed_colors))
            EXPECT(!color_set(getpid(), p->allowed_colors));

        EXPECT((fd = perfctr_open()) != -1);
        EXPECT(!utils_send_fd(sockets[1], fd));

        EXPECT(!ptrace(PTRACE_TRACEME, 0, NULL, NULL));
        execvp(VECT_ELEM(&p->argv, 0), p->argv.data);
    } else {
        int status;

        p->pid = pid;

        /* Get performance counter file descriptor from child */
        EXPECT(!utils_recv_fd(sockets[0], &p->perf_fd));
        close(sockets[0]);
        close(sockets[1]);

        /* Wait for the child to call exec */
        EXPECT(waitpid(pid, &status, 0) != -1);
        assert(!WIFEXITED(status));
        assert(WIFSTOPPED(status));
        assert(WSTOPSIG(status) == SIGTRAP);
        p->state = STOPPED;

        /* Start the performance counters */
        EXPECT(!perfctr_init(p->perf_fd, &p->cpu_control));
    }

    return 0;
}

int
process_stop(process_t *p)
{
    int status;
    assert(p->state == RUNNING);
    EXPECT(kill(p->pid, SIGSTOP) != -1);
    EXPECT(waitpid(p->pid, &status, 0) != -1);

    //XXX What if the program terminates before it recives the stop signal?
    assert(!WIFEXITED(status));
    assert(WIFSTOPPED(status));
    assert(WSTOPSIG(status) == SIGSTOP);
    p->state = STOPPED;
    return 0;
}

int
process_cont(process_t *p)
{
    assert(p->state == STOPPED);
    EXPECT(!perfctr_init(p->perf_fd, &p->cpu_control));
    EXPECT(!ptrace(PTRACE_CONT, p->pid, NULL, NULL));
    p->state = RUNNING;
    return 0;
}

int
process_kill(process_t *p)
{
    assert(p->state != EXITED);
    EXPECT(!ptrace(PTRACE_KILL, p->pid, NULL, NULL));
    p->state = EXITED;
    return 0;
}


#define ALL_FUNC_DEF(name, test)                        \
    int process_ ## name ## _all(process_vect_t *pv)    \
    {                                                   \
        process_t *iter;                                \
        VECT_FOREACH(pv, iter) {                        \
            if (test(iter->state))                      \
                EXPECT(!process_ ## name(iter));        \
        }                                               \
        return 0;                                       \
    }

#define ALWAYS_TRUE(state) 1
ALL_FUNC_DEF(launch, ALWAYS_TRUE)

#define IS_RUNNING(state) (state == RUNNING)
#define IS_STOPPED(state) (state == STOPPED)
#define NOT_EXITED(state) (state != EXITED)
ALL_FUNC_DEF(stop, IS_RUNNING)
ALL_FUNC_DEF(cont, IS_STOPPED)
ALL_FUNC_DEF(kill, NOT_EXITED)

int
process_wait(process_t *p, process_wait_state_t *state)
{
    int status;

    waitpid(p->pid, &status, 0);
    EXPECT(WIFEXITED(status) || WIFSTOPPED(status));

    if (WIFEXITED(status))
        p->state = EXITED;

    if (WIFSTOPPED(status))
        p->state = STOPPED;

    return 0;
}

static process_t *
find_process(pid_t pid, process_vect_t *pv)
{
    process_t *iter;
    VECT_FOREACH(pv, iter) {
        if (iter->pid == pid)
            return iter;
    }
    return NULL;
}

int
process_wait_all(process_vect_t *pv, process_wait_state_t *state)
{
    pid_t pid;

    do {
        int status;
        process_t *p;

        pid = waitpid(-1, &status, 0);
        EXPECT(WIFEXITED(status) || WIFSTOPPED(status));
        if (pid < 0) {
            EXPECT(errno == ECHILD);
            break;
        }
        p = find_process(pid, pv);
        EXPECT(p != NULL);

        if (WIFEXITED(status)) {
            p->state = EXITED;

            printf("Pid: %d exited", p->pid);
            if (p->leader) {
                printf(", is leader killing all processes\n");

                EXPECT(!process_stop_all(pv));
                *state = WAIT_STOPPED;
                goto out;
            }
            printf("\n");
        }

        if (WIFSTOPPED(status)) {
            p->state = STOPPED;
            switch (WSTOPSIG(status)) {
                case SIGIO:
                    EXPECT(!process_stop_all(pv));
                    *state = WAIT_PERFCTR;
                    goto out;
                default:
                    printf("Pid: %d stopped by unexpected signal: %d killing all processes\n",
                            pid, WSTOPSIG(status));
                    EXPECT(!process_stop_all(pv));
                    *state = WAIT_STOPPED;
                    goto out;
            }
        }
    } while (1);

out:
    return 0;
}


/* XXX These functions depend on the log file format. Maybe they should be moved
 * to a different file so that this file. */
static int
process_read_counter(process_t *p, log_pmc_t *pmc)
{
    struct perfctr_sum_ctrs ctrs;

    assert(p->state == EXITED || p->state == STOPPED);
    EXPECT(!_vperfctr_read_sum(p->perf_fd, &ctrs));

    pmc->tsc = ctrs.tsc;
    for (int i = 0; i < (int)p->cpu_control.nractrs && i < LOG_MAX_CTRS; i++)
        pmc->pmc[i] = ctrs.pmc[i];

    return 0;
}

int
process_read_counter_all(process_vect_t *pv, log_event_t *event)
{
    int i = 0;
    process_t *iter;

    memset(event, 0, sizeof *event);
    VECT_FOREACH(pv, iter) {
        EXPECT(i < LOG_MAX_PROCS);
        EXPECT(!process_read_counter(iter, &event->pmc[i++]));
    }
    return 0;
}
