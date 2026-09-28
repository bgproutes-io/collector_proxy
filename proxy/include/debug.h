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
}
Debugger_t;


#define LOG_LEVEL_ALWAYS    0
#define LOG_LEVEL_IMPORTANT 1
#define LOG_LEVEL_OPTIONAL  2
#define LOG_LEVEL_TOO_MUCH  3


#define DEBUG(val, ...)                                             \
    if (val <= global_debug.logLevel) {                             \
        write_logs(BLUE, "DEBUG", NULL, NULL, __VA_ARGS__);         \
    }



#define WARNING(val, ...)                                           \
    if (val <= global_debug.logLevel) {                             \
        write_logs(YELLOW, "WARNING", NULL, NULL, __VA_ARGS__);     \
    }




#define ERROR(val, ...)                                             \
    if (val <= global_debug.logLevel) {                             \
        write_logs(RED, "ERROR", NULL, NULL, __VA_ARGS__);          \
    }



#define BUG_REPORT(val, ...)                                            \
    if (val <= global_debug.logLevel) {                                 \
        write_logs(MAGENTA, "BUG_REPORT", NULL, NULL, __VA_ARGS__);     \
    }


    
    
    
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



extern void init_debug(char* debug_file, int loglevel);
extern void finish_debug();
extern void rotate_logs();
extern void debug_write_prefix(const char *color, const char *tag);
void write_logs(char* color, char* log_type, const char *fmt, ...);

extern Debugger_t global_debug;

#endif
