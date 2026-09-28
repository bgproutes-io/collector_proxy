#include "../include/debug.h"

Debugger_t global_debug;
static volatile int debug_shutting_down = 0;


void finish_debug()
{
    debug_shutting_down = 1;

    if (global_debug.debug_file && global_debug.debug_file != stderr)
    {
        fclose(global_debug.debug_file);
    }
}



void debug_write_prefix(const char *color, const char *tag)
{
    global_debug.local = localtime(&global_debug.secs);

    fprintf(global_debug.debug_file, "%s", color);

    fprintf(global_debug.debug_file,
        "%04d-%02d-%02d %02d:%02d:%02d [%s] ",
        global_debug.local->tm_year + 1900, global_debug.local->tm_mon + 1, global_debug.local->tm_mday,
        global_debug.local->tm_hour, global_debug.local->tm_min, global_debug.local->tm_sec,
        tag
    );
}



void rotate_logs()
{
    if (global_debug.debug_line_count < global_debug.debug_max_lines)
        return;

    if (global_debug.debug_file && global_debug.debug_file != stderr)
        fclose(global_debug.debug_file);

    char rotated[1100];
    snprintf(rotated, sizeof(rotated), "%s.1", global_debug.debug_filename);

    // Remove old rotated file (optional)
    remove(rotated);

    // Rename file → file.1
    rename(global_debug.debug_filename, rotated);

    // Start new log file
    global_debug.debug_file = fopen(global_debug.debug_filename, "w");
    if (!global_debug.debug_file) {
        global_debug.debug_file = stderr;
    }

    global_debug.debug_line_count = 0;
}



void init_debug(char* debug_file, int loglevel)
{
    global_debug.logLevel = loglevel;
    global_debug.debug_line_count = 0;
    global_debug.debug_max_lines = 100000;

    if (strlen(debug_file) == 0)
    {
        global_debug.debug_file = stderr;
    }
    else
    {
        strcpy(global_debug.debug_filename, debug_file);
        global_debug.debug_file = fopen(global_debug.debug_filename, "w");

        if (!global_debug.debug_file)
            global_debug.debug_file = stderr;
    }
}


static void write_logs_internal(char* color, char* log_type, const char* debug_message)
{
    rotate_logs();

    global_debug.secs = time(0);
    global_debug.local = localtime(&global_debug.secs);
    fprintf(global_debug.debug_file, "%s", color);

    fprintf(global_debug.debug_file,
        "%04d-%02d-%02d %02d:%02d:%02d [%s",
        global_debug.local->tm_year + 1900, global_debug.local->tm_mon + 1, global_debug.local->tm_mday,
        global_debug.local->tm_hour, global_debug.local->tm_min, global_debug.local->tm_sec,
        log_type
    );

    fprintf(global_debug.debug_file, "] ");

    fprintf(global_debug.debug_file, "%s", debug_message);
    fprintf(global_debug.debug_file, DEFAULT "\n");
    fflush(global_debug.debug_file);

    global_debug.debug_line_count++;
}


void write_logs(char* color, char* log_type, const char *fmt, ...)
{
    char debug_message[MAX_RECV_BUFF];
    memset(debug_message, 0, MAX_RECV_BUFF);

    va_list args;
    va_start(args, fmt);
    vsnprintf(debug_message, MAX_RECV_BUFF, fmt, args);
    va_end(args);

    write_logs_internal(color, log_type, debug_message);
    return;

}
