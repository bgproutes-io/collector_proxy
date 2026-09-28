#include <ctype.h>
#include "../include/utils.h"

/**********
 * code from https://nachtimwald.com/2019/07/10/recursive-create-directory-in-c-revisited/
*************/

void create_directory(char *dirname)
{
    const char *p;
    char       *temp;

    temp = calloc(1, strlen(dirname)+1);
    p = dirname;

    while ((p = strchr(p, '/')) != NULL) 
    {
        /* Skip empty elements. Could be a Windows UNC path or
           just multiple separators which is okay. */
        if (p != dirname && *(p-1) == '/') 
        {
            p++;
            continue;
        }
        /* Put the path up to this point into a temporary to
           pass to the make directory function. */
        memcpy(temp, dirname, p-dirname);
        temp[p-dirname] = '\0';
        p++;

        if (mkdir(temp, 0755) != 0) 
        {
            if (errno != EEXIST) 
            {
                printf("We axit create dir with an error:");
                perror("mkdir");
                break;
            }
        }
    }
    free(temp);
}



ssize_t send_no_sigpipe(int sock, const void *buf, size_t len, int flags)
{
#ifdef MSG_NOSIGNAL
    return send(sock, buf, len, flags | MSG_NOSIGNAL);
#else
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    return send(sock, buf, len, flags);
#endif
}



