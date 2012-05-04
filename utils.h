#ifndef _UTILS_H_
#define _UTILS_H_
#include <libperfctr.h>
#include <mhash.h>

int utils_setaffinity(int core);
int utils_setnumanode(int node);

int utils_send_fd(int sockfd, int fd);
int utils_recv_fd(int sockfd, int *fd);

int perfctr_open(void);
int perfctr_init(int fd, struct perfctr_cpu_control *cpu_control);
int perfctr_read(int fd, struct perfctr_sum_ctrs *ctrs);
int perfctr_resume(int fd);

typedef unsigned char utils_md5hash_t[16];
int utils_md5(const char *filename, utils_md5hash_t hash);

#endif /* _UTILS_H_ */
