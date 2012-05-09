#ifndef __EXPECT_H
#define __EXPECT_H
#include <stdio.h>

#define EXPECT_PRINT do {                           \
    fprintf(stderr, "Error: %s:%d: %s\n",           \
            __FILE__, __LINE__, strerror(errno));   \
} while (0)


#define _EXPECT(__c, cmd) do {  \
    int _c = __c;               \
    if (!_c) {                  \
        EXPECT_PRINT;           \
        cmd;                    \
    }                           \
} while (0)

#define EXPECT(__c)          _EXPECT(__c, return -1)
#define EXPECT_RET(__c, val) _EXPECT(__c, return val)
#define EXPECT_EXIT(__c)     _EXPECT(__c, exit(EXIT_FAILURE))

#endif /* __EXPECT_H */
