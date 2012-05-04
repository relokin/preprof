#include <stdio.h>
#include "cclib.h"


int
main(int argc, char **argv)
{
    char mask[CC_MASK_LEN];

    color_get(0, mask);
    printf("%s\n", mask);

    color_set(0, "ffff");
    color_get(0, mask);
    printf("%s\n", mask);
    return 0;
}
