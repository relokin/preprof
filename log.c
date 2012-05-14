#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bzlib.h>

#include "log.h"
#include "expect.h"

static const char magic[] = "LAUNCHER";

static const char *log_errstr_[] = {
    "Ok", "Parameter", "System", "Memory", "End-of-file", "Format"
};


#define LOG_DEBUG

#ifdef LOG_DEBUG
#define DEBUG(_fmt, _args...) \
    fprintf(stderr, "Error:%s:%d: " _fmt "\n", __FILE__, __LINE__, _args)
#else
#define DEBUG(_fmt, _args...)
#endif

#define E(_c, _e) do {                                  \
    if (_c) {                                           \
        DEBUG("%s", strerror(errno));                   \
        return _e;                                      \
    }                                                   \
} while (0)

#define E_LOG(log_error) do {               \
    if (log_error) {                        \
        DEBUG("%s", strerror(errno));       \
        return log_error;                   \
    }                                       \
} while (0)


static log_error_t
bzip2_init(log_t *log)
{
    int bzerror;

    if (log->mode == 'r')
        log->bzfile = BZ2_bzReadOpen(&bzerror, log->fp, 0, 0, NULL, 0);
    else
        log->bzfile = BZ2_bzWriteOpen(&bzerror, log->fp, 1, 0, 30);

    if (bzerror != BZ_OK)
        return LOG_ERROR_SYS;

    return LOG_ERROR_OK;
}

static log_error_t
bzip2_fini(log_t *log)
{
    int bzerror;

    if (log->mode == 'r')
        BZ2_bzReadClose(&bzerror, log->bzfile);
    else
        BZ2_bzWriteClose(&bzerror, log->bzfile, 0, NULL, NULL);

    if (bzerror != BZ_OK)
        return LOG_ERROR_SYS;

    return LOG_ERROR_OK;
}

static log_error_t
bzip2_read(log_t *log, void *buf, int count)
{
    int bzerror;
    int read;
    log_error_t log_error = LOG_ERROR_OK;

    /* Error handling: If BZ2_bzRead(bzerror, b, count) reads the last count
     * bytes in the file, it returns count and sets bzerror to BZ_STREAM_END.
     * In this case we want to return USF_ERROR_OK, since there are no errors,
     * and then return USF_ERROR_EOF on the next call.
     *
     * Note: If BZ2_bzRead is called once more after it has returned
     * BZ_STREAM_END it seems to always return BZ_SEQUENCE_ERROR, which
     * we could check for. However, according to the manual
     * BZ_SEQUENCE_ERROR is returned "if b was opened with BZ2_bzWriteOpen".
     *
     * --  David E.
     */

    if (log->bzeof)
        return LOG_ERROR_EOF;

    read = BZ2_bzRead(&bzerror, log->bzfile, buf, count);
    if (bzerror != BZ_OK) {
        if (bzerror == BZ_STREAM_END) {
            log->bzeof = 1;
            if (read != count)
                log_error = LOG_ERROR_FILE;
        } else
            log_error = LOG_ERROR_SYS;
    }

    return log_error;
}

static log_error_t
bzip2_write(log_t *log, const char *buf, size_t count)
{
    int bzerror;

    BZ2_bzWrite(&bzerror, log->bzfile, (void *)buf, count);
    return bzerror != BZ_OK ? LOG_ERROR_SYS : LOG_ERROR_OK;
}


log_error_t
log_open(log_t *log, const char *path)
{
    char magic_[sizeof magic];

    if (path)
        E((log->fp = fopen(path, "r")) == NULL, LOG_ERROR_SYS);
    else
        log->fp = stdin;

    E(fread(&magic_, sizeof magic_, 1, log->fp) != 1, LOG_ERROR_SYS);
    E(strncmp(magic_, magic, sizeof magic), LOG_ERROR_FILE);
    E(fread(&log->header, sizeof log->header, 1, log->fp) != 1, LOG_ERROR_SYS);
    E(log->header.version != LOG_VERSION_CURRENT, LOG_ERROR_FILE);

    log->mode = 'r';
    E_LOG(bzip2_init(log));

    return LOG_ERROR_OK;
}

log_error_t
log_create(log_t *log, log_header_t *header, const char *path)
{
    if (path)
        E((log->fp = fopen(path, "w")) == NULL, LOG_ERROR_SYS);
    else
        log->fp = stdout;

    header->version = LOG_VERSION_CURRENT;
    E(fwrite(magic, sizeof magic, 1, log->fp) != 1, LOG_ERROR_SYS);
    E(fwrite(header, sizeof *header, 1, log->fp) != 1, LOG_ERROR_SYS);
    log->header = *header;

    log->mode = 'w';
    E_LOG(bzip2_init(log));

    return LOG_ERROR_OK;
}

log_error_t
log_close(log_t *log)
{
    E_LOG(bzip2_fini(log));
    if (log->fp != stdout && log->fp != stdin)
        fclose(log->fp);
    return LOG_ERROR_OK;
}


log_error_t
log_read_event(log_t *log, log_event_t *event)
{
    return bzip2_read(log, (char *)event, sizeof *event);
}

log_error_t
log_write_event(log_t *log, log_event_t *event)
{
    return bzip2_write(log, (const char *)event, sizeof *event);
}


const char *
log_errstr(log_error_t e)
{
    return log_errstr_[e];
}

log_error_t
log_header(log_t *log, log_header_t *header)
{
    *header = log->header;
    return LOG_ERROR_OK;
}

#define NUM_PMC_FUNC(op)                                        \
void                                                            \
log_pmc_##op(log_pmc_t *r, log_pmc_t *a, log_pmc_t *b)          \
{                                                               \
    int i;                                                      \
    r->tsc = NUMOP_##op(a->tsc, b->tsc);                        \
    for (i = 0; i < LOG_MAX_CTRS; i++)                          \
        r->pmc[i] = NUMOP_##op(a->pmc[i], b->pmc[i]);           \
}

#define NUMOP_add(a, b) ((a) + (b))
#define NUMOP_sub(a, b) ((a) - (b))

NUM_PMC_FUNC(add)
NUM_PMC_FUNC(sub)

#define NUM_EVENT_FUNC(op)                                      \
void                                                            \
log_event_##op(log_event_t *r, log_event_t *a, log_event_t *b)  \
{                                                               \
    int i;                                                      \
    for (i = 0; i < LOG_MAX_PROCS; i++)                         \
        log_pmc_##op(&r->pmc[i], &a->pmc[i], &b->pmc[i]);       \
}

NUM_EVENT_FUNC(add)
NUM_EVENT_FUNC(sub)


