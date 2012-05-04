#ifndef __PROCESS_H
#define __PROCESS_H
#include <libperfctr.h>

#include "vect.h"
#include "log.h"
#include "external/cclib/cclib.h"

typedef VECT(char *) process_argv_t;

typedef enum {
    RUNNING,
    STOPPED,
    EXITED
} process_state_t;

typedef struct {
    /* Private */
    process_state_t state;
    pid_t pid;

    /* Public */
    int leader;

    int core;
    int node;

    char *stdin;
    char *stdout;
    char *stderr;

    char allowed_colors[CC_MASK_LEN];

    unsigned long start_addr;
    unsigned long stop_addr;

    unsigned long start_count;
    unsigned long stop_count;

    process_argv_t argv;

    int perf_fd;
    struct perfctr_cpu_control cpu_control;
} process_t;

typedef VECT(process_t) process_vect_t;

typedef enum {
    WAIT_STOPPED,
    WAIT_PERFCTR,
} process_wait_state_t;

int process_init(process_t *p);
int process_fini(process_t *p);

int process_launch(process_t *p);
int process_stop(process_t *p);
int process_cont(process_t *p);
int process_kill(process_t *p);
int process_wait(process_t *p, process_wait_state_t *state);

int process_launch_all(process_vect_t *pv);
int process_stop_all(process_vect_t *pv);
int process_cont_all(process_vect_t *pv);
int process_kill_all(process_vect_t *pv);
int process_wait_all(process_vect_t *pv, process_wait_state_t *state);

int process_read_counter_all(process_vect_t *pv, log_event_t *event);

#endif /* __PROCESS_H */
