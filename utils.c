#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <numa.h>

#include "utils.h"
#include "expect.h"

int
utils_setaffinity(int core)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(core, &set);
    EXPECT(!sched_setaffinity(getpid(), sizeof(cpu_set_t), &set));
    return 0;
}

int
utils_setnumanode(int node)
{
    struct bitmask *nm;
    EXPECT(numa_available() != -1);
    EXPECT(numa_bitmask_isbitset(numa_get_mems_allowed(), node));

    EXPECT((nm = numa_allocate_nodemask()) != NULL);
    numa_bitmask_clearall(nm);
    numa_bitmask_setbit(nm, node);
    numa_set_membind(nm);
    numa_free_nodemask(nm);
    return 0;
}

int
utils_send_fd(int sockfd, int fd)
{
    char buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {
        .msg_control = buf,
        .msg_controllen = sizeof(buf)
    };
    struct cmsghdr *cmsg;

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    *(int *)CMSG_DATA(cmsg) = fd;

    msg.msg_controllen = cmsg->cmsg_len;

    return sendmsg(sockfd, &msg, 0);
}

int
utils_recv_fd(int sockfd, int *fd)
{
    char buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {
        .msg_control = buf,
        .msg_controllen = sizeof(buf)
    };
    struct cmsghdr *cmsg;

    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    if (recvmsg(sockfd, &msg, 0))
        return -1;

    cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg) {
        fprintf(stderr, "Failed to receive fds, aborting.\n");
        return -1;
    }

    assert(cmsg->cmsg_type == SCM_RIGHTS);
    *fd =  *(int *)CMSG_DATA(cmsg);
    return 0;
}

int
perfctr_open(void)
{
    int fd;
    EXPECT((fd = _vperfctr_open(1)) != -1);
    EXPECT(!perfctr_abi_check_fd(fd));
    return fd;
}

int
perfctr_init(int fd, struct perfctr_cpu_control *cpu_control)
{
    struct vperfctr_control control;
    EXPECT(!perfctr_abi_check_fd(fd));
    memset(&control, 0, sizeof control);
    control.cpu_control = *cpu_control;
    control.si_signo = SIGIO;
    return _vperfctr_control(fd, &control);
}

int
perfctr_read(int fd, struct perfctr_sum_ctrs *ctrs)
{
    EXPECT(!_vperfctr_read_sum(fd, ctrs));
    return 0;
}

int
perfctr_resume(int fd)
{
    EXPECT(!_vperfctr_iresume(fd));
    return 0;
}

int
utils_md5(const char *path, utils_md5hash_t hash)
{
    int ret = 0;
    MHASH td;
    FILE *fp;

    td = mhash_init(MHASH_MD5);
    if (td == MHASH_FAILED)
        return -1;

    fp = fopen(path, "r");
    if (!fp)
        return -1;

#define BUFFER_SIZE 256
    do {
        size_t len;
        unsigned char buffer[BUFFER_SIZE];

        len = fread(buffer, sizeof(unsigned char), BUFFER_SIZE, fp);
        if (ferror(fp)) {
            ret = -1;
            goto out;
        }
        mhash(td, buffer, len);
    } while (!feof(fp));
    mhash_deinit(td, hash);

out:
    fclose(fp);
    return ret;
}
