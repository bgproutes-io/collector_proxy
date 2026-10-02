#include "../include/debug.h"
#include <sys/stat.h>
#include <sys/time.h>

#define LOG_ROTATE_MAX_BYTES (50U * 1024U * 1024U)

Debugger_t global_debug;
static volatile int debug_shutting_down = 0;

const char *log_level_name(int level)
{
    switch (level) {
        case LOG_LEVEL_ALWAYS: return "always";
        case LOG_LEVEL_IMPORTANT: return "important";
        case LOG_LEVEL_OPTIONAL: return "optional";
        case LOG_LEVEL_TOO_MUCH: return "trace";
        default: return "unknown";
    }
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void ensure_log_stream(void)
{
    if (!global_debug.debug_file)
        global_debug.debug_file = stderr;
}


void finish_debug(void)
{
    debug_shutting_down = 1;

    if (global_debug.debug_file && global_debug.debug_file != stderr)
    {
        fclose(global_debug.debug_file);
    }
    global_debug.debug_file = NULL;
}



void debug_write_prefix(const char *color, const char *tag)
{
    ensure_log_stream();
    global_debug.local = localtime(&global_debug.secs);

    if (global_debug.use_color)
        fprintf(global_debug.debug_file, "%s", color);

    fprintf(global_debug.debug_file,
        "%04d-%02d-%02d %02d:%02d:%02d [%s] ",
        global_debug.local->tm_year + 1900, global_debug.local->tm_mon + 1, global_debug.local->tm_mday,
        global_debug.local->tm_hour, global_debug.local->tm_min, global_debug.local->tm_sec,
        tag
    );
}



void rotate_logs(void)
{
    ensure_log_stream();
    if (global_debug.debug_line_count < global_debug.debug_max_lines)
        return;

    if (global_debug.debug_filename[0] == '\0') {
        global_debug.debug_line_count = 0;
        return;
    }

    if (global_debug.debug_file && global_debug.debug_file != stderr)
        fclose(global_debug.debug_file);

    char rotated[1100];
    snprintf(rotated, sizeof(rotated), "%s.1", global_debug.debug_filename);

    remove(rotated);
    if (rename(global_debug.debug_filename, rotated) != 0 && errno != ENOENT)
        fprintf(stderr, "proxy: unable to rotate log '%s': %s\n",
                global_debug.debug_filename, strerror(errno));

    global_debug.debug_file = fopen(global_debug.debug_filename, "a");
    if (!global_debug.debug_file) {
        fprintf(stderr, "proxy: unable to reopen log '%s': %s; using stderr\n",
                global_debug.debug_filename, strerror(errno));
        global_debug.debug_file = stderr;
    }

    global_debug.debug_line_count = 0;
}



void init_debug(const char* debug_file, int loglevel)
{
    memset(&global_debug, 0, sizeof(global_debug));
    debug_shutting_down = 0;
    if (loglevel < LOG_LEVEL_ALWAYS)
        loglevel = LOG_LEVEL_ALWAYS;
    if (loglevel > LOG_LEVEL_TOO_MUCH)
        loglevel = LOG_LEVEL_TOO_MUCH;
    global_debug.logLevel = loglevel;
    global_debug.debug_line_count = 0;
    global_debug.debug_max_lines = 100000;

    if (!debug_file || debug_file[0] == '\0')
    {
        global_debug.debug_file = stderr;
    }
    else
    {
        snprintf(global_debug.debug_filename,
                 sizeof(global_debug.debug_filename), "%s", debug_file);
        global_debug.debug_file = fopen(global_debug.debug_filename, "a");

        if (!global_debug.debug_file) {
            fprintf(stderr, "proxy: unable to open log '%s': %s; using stderr\n",
                    global_debug.debug_filename, strerror(errno));
            global_debug.debug_file = stderr;
        }
    }
    global_debug.use_color = isatty(fileno(global_debug.debug_file));

    if (global_debug.debug_filename[0] != '\0') {
        struct stat status;
        if (stat(global_debug.debug_filename, &status) == 0 &&
            status.st_size > LOG_ROTATE_MAX_BYTES) {
            global_debug.debug_line_count = global_debug.debug_max_lines;
            rotate_logs();
        }
    }
}

static void write_logs_internal(const char* color, const char* log_type,
                                const char* file, int line,
                                const char* function,
                                const char* debug_message)
{
    if (debug_shutting_down)
        return;
    rotate_logs();

    struct timeval now;
    gettimeofday(&now, NULL);
    global_debug.secs = now.tv_sec;
    struct tm local_time;
    localtime_r(&global_debug.secs, &local_time);
    char timestamp[32];
    char timezone[8];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &local_time);
    strftime(timezone, sizeof(timezone), "%z", &local_time);

    if (global_debug.use_color)
        fprintf(global_debug.debug_file, "%s", color);
    fprintf(global_debug.debug_file, "%s.%03ld%s [%s] [pid=%ld]",
            timestamp, now.tv_usec / 1000, timezone, log_type, (long)getpid());
    if (file && function)
        fprintf(global_debug.debug_file, " [%s:%d %s]",
                base_name(file), line, function);
    fprintf(global_debug.debug_file, " %s", debug_message);
    if (global_debug.use_color)
        fprintf(global_debug.debug_file, DEFAULT);
    fputc('\n', global_debug.debug_file);
    fflush(global_debug.debug_file);

    global_debug.debug_line_count++;
}


void write_logs(const char* color, const char* log_type, const char *fmt, ...)
{
    char debug_message[MAX_RECV_BUFF];
    memset(debug_message, 0, MAX_RECV_BUFF);

    va_list args;
    va_start(args, fmt);
    vsnprintf(debug_message, MAX_RECV_BUFF, fmt, args);
    va_end(args);

    write_logs_internal(color, log_type, NULL, 0, NULL, debug_message);
}

void write_logs_at(const char* color, const char* log_type,
                   const char* file, int line, const char* function,
                   const char* fmt, ...)
{
    char debug_message[MAX_RECV_BUFF] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(debug_message, sizeof(debug_message), fmt, args);
    va_end(args);
    write_logs_internal(color, log_type, file, line, function, debug_message);
}
