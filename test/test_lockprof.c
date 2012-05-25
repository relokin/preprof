#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#define MAX_THREADS 16

typedef struct {
    int threads;
} args_t;

#define EXPECT(_c) do {                         \
    if (!(_c)) {                                \
        fprintf(stderr, "Error: %d: %s\n",      \
                __LINE__, strerror(errno));     \
        exit(EXIT_FAILURE);                     \
    }                                           \
} while (0)

#define DELAY(d) do {                   \
    int i;                              \
    for (i = 0; i < (d); i++)           \
        __asm__ volatile ("nop");       \
} while (0)

static pthread_mutex_t mutex;
static pthread_barrier_t barrier;

static int
parse_args(args_t *args, int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage %s <threads>\n", argv[0]);
        return -1;
    }

    args->threads = atoi(argv[1]);
    args->threads = args->threads < MAX_THREADS ? args->threads : MAX_THREADS;
    return 0;
}

#define PRIVATE_COMPUTE 10000
#define CRITCAL_COMPUTE 100

static void *
do_test(void *args)
{
    int i;

    for (i = 0; i < 1000000; i++) {
        DELAY(PRIVATE_COMPUTE);

        pthread_mutex_lock(&mutex);
        DELAY(CRITCAL_COMPUTE);
        pthread_mutex_unlock(&mutex);

        if (!(i & 0xff))
            pthread_barrier_wait(&barrier);
    }

    return NULL;
}

int
main(int argc, char **argv)
{
    args_t args;
    pthread_t threads[MAX_THREADS];
    int i;

    EXPECT(!parse_args(&args, argc, argv));

    EXPECT(!pthread_mutex_init(&mutex, NULL));
    EXPECT(!pthread_barrier_init(&barrier, NULL, args.threads));

    for (i = 0; i < args.threads; i++)
        EXPECT(!pthread_create(&threads[i], NULL, do_test, &i));

    for (i = 0; i < args.threads; i++)
        EXPECT(!pthread_join(threads[i], NULL));

    EXPECT(!pthread_mutex_destroy(&mutex));
    EXPECT(!pthread_barrier_destroy(&barrier));

    return 0;
}
