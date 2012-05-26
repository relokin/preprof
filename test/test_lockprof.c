#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "../tsc.h"

#define MAX_THREADS 16

typedef struct {
    int threads;
} args_t;

typedef struct {
    unsigned long count;
    unsigned long cycles;
} thread_return_t;

static __thread thread_return_t thread_return;

#define EXPECT(_c) do {                         \
    if (!(_c)) {                                \
        fprintf(stderr, "Error: %d: %s\n",      \
                __LINE__, strerror(errno));     \
        exit(EXIT_FAILURE);                     \
    }                                           \
} while (0)


static inline unsigned long
delay(int delay)
{
    unsigned long i;
    for (i = 0; i < delay; i++)
        ;
    return i;
}


#define LOCK    1
#define BARRIER 1

#if LOCK == 0
#  define lock
#  define lock_init(lock)   0
#  define lock_fini(lock)   0
#  define lock_lock(lock)   0
#  define lock_unlock(lock) 0 
#elif LOCK == 1 
   static pthread_mutex_t lock;
#  define lock_init(lock)   pthread_mutex_init(lock, NULL)
#  define lock_fini(lock)   pthread_mutex_destroy(lock)
#  define lock_lock(lock)   pthread_mutex_lock(lock)
#  define lock_unlock(lock) pthread_mutex_unlock(lock)
#elif LOCK == 2
   static pthread_spinlock_t lock;
#  define lock_init(lock)   pthread_spin_init(lock, PTHREAD_PROCESS_SHARED)
#  define lock_fini(lock)   pthread_spin_destroy(lock)
#  define lock_lock(lock)   pthread_spin_lock(lock)
#  define lock_unlock(lock) pthread_spin_unlock(lock)
#endif

#if BARRIER == 0
#  define barrier
#  define barrier_init(barrier, threads) 0
#  define barrier_fini(barrier)          0
#  define barrier_wait(barrier)          0
#elif BARRIER == 1
   static pthread_barrier_t barrier;
#  define barrier_init(barrier, threads) pthread_barrier_init(barrier, NULL, threads)
#  define barrier_fini(barrier)          pthread_barrier_destroy(barrier)
#  define barrier_wait(barrier)          pthread_barrier_wait(barrier)
#endif

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
#define CRITCAL_COMPUTE 10

static void *
do_test(void *args)
{
    int i;
    unsigned long count = 0;
    unsigned long tsc;

    tsc = read_tsc_p();
    for (i = 0; i < 1000000; i++) {
        count += delay(PRIVATE_COMPUTE);

        lock_lock(&lock);
        count += delay(CRITCAL_COMPUTE);
        lock_unlock(&lock);

        if (!(i & 0xff))
            barrier_wait(&barrier);
    }

    thread_return.count = count;
    thread_return.cycles = read_tsc_p() - tsc;
    return &thread_return;
}

int
main(int argc, char **argv)
{
    args_t args;
    pthread_t threads[MAX_THREADS];
    thread_return_t *ret[MAX_THREADS];
    int i;

    EXPECT(!parse_args(&args, argc, argv));

    EXPECT(!lock_init(&lock));
    EXPECT(!barrier_init(&barrier, args.threads));

    for (i = 0; i < args.threads; i++)
        EXPECT(!pthread_create(&threads[i], NULL, do_test, &i));

    for (i = 0; i < args.threads; i++)
        EXPECT(!pthread_join(threads[i], (void *)&ret[i]));

    for (i = 0; i < args.threads; i++)
        printf("thread %d: count: %lu, cycles: %lu\n", i, ret[i]->count, ret[i]->cycles);

    EXPECT(!lock_fini(&lock));
    EXPECT(!barrier_fini(&barrier));

    return 0;
}
