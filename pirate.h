#ifndef _STRESS_H_
#define _STRESS_H_
#include <libperfctr.h>

#define PIRATE_MAX_PROC 4

typedef struct {
    int processes;
    size_t footprint;
    int cores[PIRATE_MAX_PROC];
    int node;
    struct perfctr_cpu_control cpu_control;
} pirate_conf_t;

int pirate_init(pirate_conf_t *conf);
int pirate_fini(void);

int pirate_launch(void);

int pirate_warm(void);
int pirate_cont(void);
int pirate_stop(void);
int pirate_kill(void);

int pirate_ctrs_read(struct perfctr_sum_ctrs *ctrs);

#endif /* _STRESS_H_ */
