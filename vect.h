#ifndef VECT_H
#define VECT_H
#include <stdlib.h>

#define VECT(type)      \
    struct {            \
        type *data;     \
        int   used;     \
        int   size;     \
    }

#define VECT_INIT(vect) do {    \
    (vect)->data = NULL;        \
    (vect)->used = 0;           \
    (vect)->size = 0;           \
} while (0)

#define VECT_NULL {NULL, 0, 0}

#define VECT_FINI(vect) do {        \
    if ((vect)->data != NULL) {     \
        free((vect)->data);         \
    }                               \
    (vect)->used = 0;               \
    (vect)->size = 0;               \
} while (0)

#define VECT_ELEM_SIZE(vect)    sizeof(*((vect)->data))
#define VECT_USED(vect)         ((vect)->used)
#define VECT_SIZE(vect)         ((vect)->size)
#define VECT_ELEM(vect, idx)    ((vect)->data[idx])
#define VECT_LAST(vect)         (VECT_ELEM(vect, VECT_USED(vect) - 1))

#define _VECT_GROW(vect, _size) do {                                    \
    (vect)->size = _size;                                               \
    (vect)->data = (__typeof__((vect)->data))realloc(                   \
            (vect)->data, (vect)->size * VECT_ELEM_SIZE(vect));         \
    if (!((vect)->data))                                                \
        abort();                                                        \
} while (0)

#define VECT_GROW(vect) _VECT_GROW(vect, ((vect)->size ? 2 * (vect)->size : 4))

#define VECT_APPEND(vect, elem) do {        \
    if ((vect)->used == (vect)->size) {     \
        VECT_GROW(vect);                    \
    }                                       \
    (vect)->data[(vect)->used++] = elem;    \
} while (0)

#define VECT_CLEAR(vect) do {   \
    (vect)->used = 0;           \
} while (0)

#define VECT_FOREACH(vect, iter) \
    for (int _i = 0; _i < (vect)->used && (iter = &VECT_ELEM(vect, _i), 1); _i++)


#define VECT_LIN_SEARCH(vect, iter, comp_func, comp_data) do {  \
    VECT_FOREACH(vect, iter) {                                  \
        if (!comp_func(&iter, comp_data))                       \
            break;                                              \
    }                                                           \
} while (0)

#define VECT_BIN_SEARCH(vect, iter, comp_func, comp_data) do {  \
    int l = 0;                                                  \
    int r = VECT_USED(vect);                                    \
    int m = (l + r) / 2;                                        \
    while (1) {                                                 \
        iter = VECT_ELEM(vect, m);                              \
        int comp = comp_func(&iter, comp_data);                 \
        if (comp > 0)                                           \
            r = m;                                              \
        else if (comp < 0)                                      \
            l = m;                                              \
        else                                                    \
            break;                                              \
        m = (l + r) / 2;                                        \
    }                                                           \
} while (0)

#endif /* VECT_H */
