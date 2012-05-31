#ifndef _LOG_H_
#define _LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <bzlib.h>

#define LOG_MAX_CTRS            8
#define LOG_MAX_PROCS           16
#define LOG_MAX_ARGC            32
#define LOG_MAX_ARG_LEN         512
#define LOG_MAX_STDIO_LEN       512

typedef enum {
    LOG_ERROR_OK = 0,
    LOG_ERROR_PARAM,
    LOG_ERROR_SYS,
    LOG_ERROR_MEM,
    LOG_ERROR_EOF,
    LOG_ERROR_FILE,
} log_error_t;

#define LOG_VERSION_CURRENT 3
typedef uint16_t log_version_t;

typedef struct {
    uint32_t counters;
    uint32_t eventsel_map[LOG_MAX_CTRS];
    uint32_t offcore_rsp0;
    uint64_t ireset;
} log_pmc_map_t;

typedef struct {
    uint64_t tsc;
    uint64_t pmc[LOG_MAX_CTRS];
} log_pmc_t;

typedef struct {
    int core;
    int node;

    char stdin[LOG_MAX_STDIO_LEN];
    char stdout[LOG_MAX_STDIO_LEN];
    char stderr[LOG_MAX_STDIO_LEN];

    int argc;
    char argv[LOG_MAX_ARGC][LOG_MAX_ARG_LEN];

    log_pmc_map_t pmc_map;
} log_header_process_t;

typedef struct {
    log_version_t version;

    int num_processes;
    log_header_process_t processes[LOG_MAX_PROCS];
} log_header_t;

typedef struct {
    int num_processes;
    log_pmc_t pmc[LOG_MAX_PROCS];
} log_event_t;

typedef struct {
    FILE *fp;
    BZFILE *bzfile;
    int bzeof;
    int mode;
    log_header_t header;
} log_t;


log_error_t log_open(log_t *log, const char *path);
log_error_t log_create(log_t *log, log_header_t *header, const char *path);
log_error_t log_close(log_t *log);

log_error_t log_header(log_t *log, log_header_t *header);
log_error_t log_read_event(log_t *log, log_event_t *event);
log_error_t log_write_event(log_t *log, log_event_t *event);

const char *log_errstr(log_error_t e);

void log_pmc_add(log_pmc_t *r, log_pmc_t *a, log_pmc_t *b);
void log_pmc_sub(log_pmc_t *r, log_pmc_t *a, log_pmc_t *b);

void log_event_add(log_event_t *r, log_event_t *a, log_event_t *b);
void log_event_sub(log_event_t *r, log_event_t *a, log_event_t *b);

#ifdef __cplusplus
}
#endif

#endif /* _LOG_H_ */
