#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "cclib.h"


#define TMPBUF_LEN 128
static int
open_proc(pid_t pid, int flags)
{
    char path[TMPBUF_LEN];

    snprintf(path, TMPBUF_LEN, "/proc/%d/color_colors", pid);

    return open(path, flags, 0);
}

#define open_proc_rd(pid) open_proc(pid, O_RDONLY)
#define open_proc_wr(pid) open_proc(pid, O_WRONLY)

int
color_set(pid_t pid, const char *str)
{
    int fd, rc = 0;

    fd = open_proc_wr(pid);
    if (fd < 0) {
        perror(NULL);
        return -1;
    }

    if (write(fd, str, CC_MASK_LEN) != CC_MASK_LEN)
        rc = -1;

    close(fd);
    return rc;
}

int
color_get(pid_t pid, char *str)
{
    int fd, rc = 0;

    fd = open_proc_rd(pid);
    if (fd < 0) {
        perror(NULL);
        return -1;
    }

    if (read(fd, str, CC_MASK_LEN) != CC_MASK_LEN)
        rc = -1;

    close(fd);
    return rc;
}
