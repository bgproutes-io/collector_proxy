#ifndef __UTILS_H__
#define __UTILS_H__

#define _GNU_SOURCE
#include "common.h"
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#ifdef __linux__
#include <netinet/tcp.h>
#endif

#define SA struct sockaddr
#define SS struct sockaddr_storage

/**
 * @file utils.h
 * @brief Miscellaneous helper utilities used throughout the project.
 *
 * This module contains generic helper functions for:
 *   - recursive directory creation
 *   - byte-order conversions (store16/store32/create32)
 *   - safe string parsing
 *   - debug hex dumps
 *   - portable integer hashing
 *   - portable case-insensitive substring search
 *   - filesystem metadata retrieval
 *   - small arithmetic helpers
 *
 * These helpers are intentionally lightweight and dependency-free.
 */


/* =========================================================================
 * Filesystem utilities
 * ========================================================================= */

/**
 * @brief Recursively create directories for a full path.
 *
 * Example: "data/bgp/2024/01/" creates all components if missing.
 *
 * @param path Directory path (modified only internally).
 *
 * @note This function ignores duplicate slashes and already-existing dirs.
 * @note Stops on first mkdir() error other than EEXIST.
 */
void create_directory(char *path);


ssize_t send_no_sigpipe(int sock, const void *buf, size_t len, int flags);

/** Parse an IPv4 or IPv6 literal into sockaddr_storage. */
int ip_to_sockaddr(const char *ip_str, SS *addr, int port);

int sockaddr_set_port(SS *addr, int listen_port);


#endif /* __UTILS_H__ */
