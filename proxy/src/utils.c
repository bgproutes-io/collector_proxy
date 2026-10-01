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


int ip_to_sockaddr(const char *ip_str, SS *addr, int port)
{
    if (!ip_str || !addr)
        return -1;

    memset(addr, 0, sizeof(*addr));

    struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
    if (inet_pton(AF_INET, ip_str, &addr4->sin_addr) == 1) {
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons((uint16_t)port);
        return 0;
    }

    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
    if (inet_pton(AF_INET6, ip_str, &addr6->sin6_addr) == 1) {
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons((uint16_t)port);
        return 0;
    }

    memset(addr, 0, sizeof(*addr));
    return -1;
}



int sockaddr_set_port(SS *addr, int listen_port)
{
    if (!addr)
    {
        return -1;
    }

    if (addr->ss_family == AF_INET)
    {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
        addr_in->sin_port = htons(listen_port);
        return 0;
    }

    else
    {
        struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
        addr_in6->sin6_port = htons(listen_port);
        return 0;
    }

    /* Invalid IP address format */
    return -1;
}

