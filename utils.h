#ifndef _UTILS_H_
#define _UTILS_H_
#include <libperfctr.h>
#include <mhash.h>

#include "vect.h"
#include "log.h"

typedef VECT(unsigned int) event_vect_t;

void *utils_calloc(size_t nmemb, size_t size);
void  utils_free(void *addr, size_t nmemb, size_t size);

int utils_setaffinity(int core);
int utils_setnumanode(int node);

int utils_send_fd(int sockfd, int fd);
int utils_recv_fd(int sockfd, int *fd);

int perfctr_open(void);
int perfctr_init(int fd, struct perfctr_cpu_control *cpu_control);
int perfctr_read(int fd, struct perfctr_sum_ctrs *ctrs);
int perfctr_resume(int fd);
void perfctr_control_init(struct perfctr_cpu_control *ctrl,
        event_vect_t *event_vect,
        unsigned int offcore_rsp0,
        unsigned int ievent,
        unsigned long icount);

typedef unsigned char utils_md5hash_t[16];
int utils_md5(const char *filename, utils_md5hash_t hash);

void log_header_append(log_header_t *header, struct perfctr_cpu_control *cntrl);
void log_event_append(log_event_t *event, struct perfctr_cpu_control *ctrl,
        struct perfctr_sum_ctrs *ctrs);

#endif /* _UTILS_H_ */
