#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include "common.h"


typedef struct debugger_s
{
    FILE* debug_file;
    char debug_filename[1024];
    time_t secs;
    struct tm *local;
    int logLevel;
    int debug_line_count;
    int debug_max_lines;
    bool use_color;
}
Debugger_t;

/*
 * ALWAYS: startup/fatal conditions that must never be hidden.
 * IMPORTANT: connection state, data loss risk, and automatic recovery.
 * OPTIONAL: retry detail and administrative activity.
 * TOO_MUCH: per-message and partial-buffer diagnostics.
 */
#define LOG_LEVEL_ALWAYS    0
#define LOG_LEVEL_IMPORTANT 1
#define LOG_LEVEL_OPTIONAL  2
#define LOG_LEVEL_TOO_MUCH  3


#define LOG_AT(level, color, tag, ...)                                  \
    do {                                                                 \
        if ((level) <= global_debug.logLevel)                            \
            write_logs_at((color), (tag), __FILE__, __LINE__, __func__, \
                          __VA_ARGS__);                                   \
    } while (0)

#define DEBUG(level, ...)      LOG_AT(level, BLUE, "DEBUG", __VA_ARGS__)
#define INFO(level, ...)       LOG_AT(level, GREEN, "INFO", __VA_ARGS__)
#define WARNING(level, ...)    LOG_AT(level, YELLOW, "WARNING", __VA_ARGS__)
#define ERROR(level, ...)      LOG_AT(level, RED, "ERROR", __VA_ARGS__)
#define BUG_REPORT(level, ...) LOG_AT(level, MAGENTA, "BUG_REPORT", __VA_ARGS__)


    
    
    
#define CHECK(x)                                                                                \
    do {                                                                                        \
        if ((x) == -1)                                                                          \
        {                                                                                       \
            ERROR_LOCAL(LOG_LEVEL_ALWAYS, "%s:%d: %s", __func__, __LINE__, strerror(errno));          \
        }                                                                                       \
    } while (0)


#define CHECK_AND_RETURN(x, ret)                                                                \
    do {                                                                                        \
        if ((x) == -1)                                                                          \
        {                                                                                       \
            ERROR_LOCAL(LOG_LEVEL_ALWAYS, "%s:%d: %s", __func__, __LINE__, strerror(errno));          \
            return ret;                                                                         \
        }                                                                                       \
    } while (0)



extern void init_debug(const char* debug_file, int loglevel);
extern void finish_debug(void);
extern void rotate_logs(void);
extern void debug_write_prefix(const char *color, const char *tag);
void write_logs(const char* color, const char* log_type, const char *fmt, ...);
void write_logs_at(const char* color, const char* log_type,
                   const char* file, int line, const char* function,
                   const char* fmt, ...);
const char *log_level_name(int level);

extern Debugger_t global_debug;

#endif
