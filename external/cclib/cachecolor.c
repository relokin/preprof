#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "cclib.h"

static int exec_argc;
static char **exec_argv;
static char colors[CC_MASK_LEN];

#define EXPECT(_x) do {                                 \
    int x = _x;                                         \
    if (!x) {                                           \
        fprintf(stderr, "Error: %s: %d: %s\n",          \
                __FILE__, __LINE__, strerror(errno));   \
        exit(EXIT_FAILURE);                             \
    }                                                   \
} while (0)


static int
parse_args(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s [COLORS] command\n", argv[0]);
        return -1;
    }

    strncpy(colors, argv[1], CC_MASK_LEN);
    colors[CC_MASK_LEN - 1] = '\0';

    exec_argc = argc - 2;
    exec_argv = argv + 2;
    return 0;
}

int
main(int argc, char **argv)
{
    pid_t pid;

    if (parse_args(argc, argv))
        return -1;

    EXPECT(color_set(getpid(), colors) != -1);

    EXPECT((pid = fork()) != -1);
    if (!pid) {
        execvp(exec_argv[0], exec_argv);
    } else {
        int status;
        do {
            EXPECT(wait(&status) != -1);
        } while (!WIFEXITED(status));
        printf("Process %d exited with status: %d\n", pid, WEXITSTATUS(status));
    }

    return 0;
}
