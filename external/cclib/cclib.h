#ifndef _CCLIB_H_
#define _CCLIB_H_
#include <sys/types.h> /* pid_t */

#define CC_COLORS       (128)
#define CC_MASK_LEN     (CC_COLORS / 4 + CC_COLORS / 32 - 1)

int color_set(pid_t pid, const char *str);
int color_get(pid_t pid,       char *str);

#endif /* _CCLIB_H_ */
