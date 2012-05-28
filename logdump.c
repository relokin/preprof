#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>
#include "log.h"

#define UNUSED(x) __attribute__ ((unused)) (x)

static const char *usage_str = 
"Usage: logdump [OPTION]... [FILE]\n\n"
"  --help,       Print this message\n"
"  --header, -h  Print header\n"
"  --all, -a     Print all events\n"
"  --sum, -s     Print sum of all events\n";

#define E(e) do {                               \
    log_error_t _e = e;                         \
    if (_e != LOG_ERROR_OK) {                   \
        fprintf(stderr, "%s\n", log_errstr(e)); \
        exit(EXIT_FAILURE);                     \
    }                                           \
} while (0)


typedef struct {
    char *filename;
    int print_header;
    int print_all;
    int print_sum;
} args_t;

static args_t args;

static int
args_parse(int argc, char **argv)
{
    static const char *short_opts = "has";
    static const struct option long_opts[] = {
        {"help",   0, NULL, 'H'},
        {"header", 0, NULL, 'h'},
        {"all",    0, NULL, 'a'},
        {"sum",    0, NULL, 's'},
    };

    memset(&args, 0, sizeof args);
    for (;;) {
        switch (getopt_long(argc, argv, short_opts, long_opts, NULL)) {
            case -1:
                if (optind < argc)
                    args.filename = argv[optind];
                break;
            case 'H':
                printf("%s", usage_str);
                exit(EXIT_SUCCESS);
            case 'h':
                args.print_header = 1;
                continue;
            case 'a':
                args.print_all = 1;
                continue;
            case 's':
                args.print_sum = 1;
                continue;
        }
        break;
    }
    return 0;
}

static void
add_pmc(log_pmc_t *sum, log_pmc_t *add)
{
    int i;
    for (i = 0; i < 8; i++)
        sum->pmc[i] += add->pmc[i];
    sum->tsc += add->tsc;
}

static void
add_event(log_event_t *sum,
          log_event_t *event)
{
    for (int i = 0; i < LOG_MAX_PROCS; i++)
        add_pmc(&sum->pmc[i], &event->pmc[i]);
}


static void
print_pmc_map(log_pmc_map_t *map)
{
    unsigned int i;
    for (i = 0; i < map->counters; i++) {
        printf("    map[%d] = 0x%.8x", i, map->eventsel_map[i]);
        if (map->eventsel_map[i] == 0x4101b7)
            printf(" (offcore_rsp0 = 0x%.4x)\n", map->offcore_rsp0);
        else if (map->eventsel_map[i] & (1<<20))
            printf(" (icount = %lu)\n", (1L << 32) - map->ireset);
        else
            printf("\n");
    }
}

static void
print_header_process(log_header_process_t *p)
{
    for (int i = 0; i < p->argc; i++)
        printf("    argv[%d]: %s\n", i, p->argv[i]);
    printf("\n");

    printf("    stdin:  %s\n", p->stdin);
    printf("    stdout: %s\n", p->stdout);
    printf("    stderr: %s\n", p->stderr);
    printf("\n");

    printf("    core: %d", p->core);
    if (p->core == -1)
        printf(" (not specified)");
    printf("\n");

    printf("    node: %d", p->node);
    if (p->node == -1)
        printf(" (not specified)");
    printf("\n");
    printf("\n");

    print_pmc_map(&p->pmc_map);
}

static void
print_header(log_header_t *h)
{
    printf("File Version: %d\n\n", h->version);
    for (int i = 0; i < h->num_processes; i++) {
        printf("process[%d] {\n", i);
        print_header_process(&h->processes[i]);
        printf("}\n\n");
    }
}

static void
print_pmc(const char *prefix, log_pmc_map_t *map, log_pmc_t *pmc)
{
    unsigned int i;
    printf("%s tsc: %lu\n", prefix, pmc->tsc);
    for (i = 0; i < map->counters; i++)
        printf("%s 0x%.8x: %lu\n", prefix, map->eventsel_map[i], pmc->pmc[i]);
}


static void
print_event(const char *prefix,
            log_header_t *header,
            log_event_t *event)
{
    printf("%s {\n", prefix);
    for (int i = 0; i < header->num_processes; i++) {
        printf("    process[%d] {\n", i);
        print_pmc("        ", &header->processes[i].pmc_map, &event->pmc[i]);
        printf("    }\n");
    }
    printf("}\n");
}

static int
print(void)
{
    log_t log;
    log_header_t header;
    log_event_t sum;
    unsigned int count = 0;

    memset(&sum, 0, sizeof(sum));

    E(log_open(&log, args.filename));
    E(log_header(&log, &header));

    if (args.print_header)
        print_header(&header);

    for (;;) {
        log_error_t error;
        log_event_t event;
        char prefix[64];

        error = log_read_event(&log, &event);
        if (error == LOG_ERROR_EOF)
            break;
        E(error);

        if (args.print_all) {
            snprintf(prefix, 64, "event[%d]", count++);
            print_event(prefix, &header, &event);
        }
        add_event(&sum, &event);
    }

    if (args.print_sum)
        print_event("total", &header, &sum);

    log_close(&log);
    return 0;
}

int
main(int argc, char **argv)
{
    args_parse(argc, argv);
    return print();
}
