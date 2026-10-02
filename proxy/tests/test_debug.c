#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"

int main(void)
{
    char path[] = "/tmp/proxy-debug-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    init_debug(path, LOG_LEVEL_IMPORTANT);
    INFO(LOG_LEVEL_IMPORTANT, "collector connected on attempt=%d", 2);
    DEBUG(LOG_LEVEL_OPTIONAL, "hidden optional detail");
    finish_debug();

    FILE *log = fopen(path, "r");
    assert(log);
    char contents[4096] = {0};
    assert(fread(contents, 1, sizeof(contents) - 1, log) > 0);
    fclose(log);
    unlink(path);

    assert(strstr(contents, "[INFO]"));
    assert(strstr(contents, "[pid="));
    assert(strstr(contents, "[test_debug.c:"));
    assert(strstr(contents, "collector connected on attempt=2"));
    assert(!strstr(contents, "hidden optional detail"));
    assert(!strstr(contents, "\x1b["));
    return 0;
}
