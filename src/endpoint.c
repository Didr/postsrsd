/* PostSRSd - Sender Rewriting Scheme daemon for Postfix
 * Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "endpoint.h"

#include "postsrsd_build_config.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_FCNTL_H
#    include <fcntl.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#    include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#    include <sys/socket.h>
#endif
#ifdef HAVE_SYS_UN_H
#    include <sys/un.h>
#endif
#ifdef HAVE_NETDB_H
#    include <netdb.h>
#endif
#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#endif

#if defined(AF_UNIX)
#    define HAVE_UNIX_SOCKETS 1
#endif
#if defined(HAVE_NETDB_H) && defined(AF_UNSPEC) && defined(AF_INET) \
    && defined(AF_INET6)
#    define HAVE_INET_SOCKETS 1
#endif
#ifndef SO_REUSEPORT
#    define SO_REUSEPORT SO_REUSEADDR
#endif
#define POSTSRSD_SOCKET_LISTEN_QUEUE 16

#ifdef HAVE_UNIX_SOCKETS
static int create_unix_socket(const char* path)
{
    struct sockaddr_un sa;
    int flags;
    if (path == NULL || *path == 0)
    {
        fprintf(stderr, "postsrsd: expected file path for unix socket\n");
        return -1;
    }
    if (acquire_lock(path) > 0)
        unlink(path);
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        goto fail;
    sa.sun_family = AF_UNIX;
    memset(sa.sun_path, 0, sizeof(sa.sun_path));
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
    if (bind(sock, (const struct sockaddr*)&sa, sizeof(struct sockaddr_un)) < 0)
        goto fail;
    if (listen(sock, POSTSRSD_SOCKET_LISTEN_QUEUE) < 0)
        goto fail;
#    ifdef HAVE_FCNTL_H
    if ((flags = fcntl(sock, F_GETFL, 0)) < 0)
        goto fail;
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
        goto fail;
#    endif
    return sock;
fail:
    fprintf(stderr, "postsrsd: %s\n", strerror(errno));
    if (sock >= 0)
        close(sock);
    return -1;
}
#endif

#ifdef HAVE_INET_SOCKETS
static int create_inet_sockets(char* addr, int family, int max_fds, int* fds)
{
    const int one = 1;
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(struct addrinfo));
    char* node = addr;
    char* service = NULL;
    if (addr[0] == '[')
    {
        node = ++addr;
        while (*addr != ']')
        {
            if (*addr == 0)
            {
                fprintf(stderr,
                        "postsrsd: expected closing ']' in socket address\n");
                return -1;
            }
            ++addr;
        }
        *addr++ = 0;
        if (*addr != ':')
        {
            fprintf(stderr,
                    "postsrsd: expected ':' separator in socket address\n");
            return -1;
        }
        service = ++addr;
    }
    else
    {
        service = strchr(addr, ':');
        if (service)
        {
            *service = 0;
            ++service;
        }
        else
        {
            service = addr;
            node = NULL;
        }
    }
    if (service == NULL || *service == 0)
    {
        fprintf(stderr, "postsrsd: expected port number in socket address\n");
        return -1;
    }
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    if (node != NULL && strcmp(node, "*") == 0)
    {
        node = NULL;
        hints.ai_flags |= AI_PASSIVE;
    }
    if (node != NULL && strcmp(node, "localhost") == 0)
    {
        node = NULL;
    }
    int err = getaddrinfo(node, service, &hints, &ai);
    if (err != 0)
    {
        fprintf(stderr, "postsrsd: %s\n", gai_strerror(err));
        return -1;
    }
    int sock = -1, count = 0, flags;
    for (struct addrinfo* it = ai; it; it = it->ai_next)
    {
        if (max_fds == 0)
            break;
        sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock < 0)
            goto fail;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0)
            goto fail;
        if (bind(sock, it->ai_addr, it->ai_addrlen) < 0)
            goto fail;
        if (listen(sock, POSTSRSD_SOCKET_LISTEN_QUEUE) < 0)
            goto fail;
#    ifdef HAVE_FCNTL_H
        if ((flags = fcntl(sock, F_GETFL, 0)) < 0)
            goto fail;
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
            goto fail;
#    endif
        *fds++ = sock;
        max_fds--;
        count++;
        continue;
fail:
        err = errno;
        fprintf(stderr, "postsrsd: %s\n", strerror(err));
        if (sock >= 0)
            close(sock);
    }
    freeaddrinfo(ai);
    if (count == 0 && err != 0)
        return -1;
    return count;
}
#endif

int endpoint_create(const char* s, int max_fds, int* fds)
{
    if (max_fds < 1)
        return 0;
#ifdef HAVE_UNIX_SOCKETS
    const char* path = NULL;
    if (strncmp(s, "unix:", 5) == 0)
    {
        path = &s[5];
    }
    else if (strncmp(s, "local:", 6) == 0)
    {
        path = &s[6];
    }
    if (path)
    {
        int fd = create_unix_socket(path);
        if (fd < 0)
        {
            fprintf(stderr, "postsrsd: failed to create endpoint '%s'\n", s);
            return -1;
        }
        *fds = fd;
        return 1;
    }
#endif
#ifdef HAVE_INET_SOCKETS
    char* addr = NULL;
    int family = AF_UNSPEC;
    if (strncmp(s, "inet:", 5) == 0)
    {
        addr = strdup(&s[5]);
    }
    else if (strncmp(s, "inet4:", 6) == 0)
    {
        addr = strdup(&s[6]);
        family = AF_INET;
    }
    else if (strncmp(s, "inet6:", 6) == 0)
    {
        addr = strdup(&s[6]);
        family = AF_INET6;
    }
    if (addr)
    {
        int ret = create_inet_sockets(addr, family, max_fds, fds);
        free(addr);
        if (ret < 0)
        {
            fprintf(stderr, "postsrsd: failed to create endpoint '%s'\n", s);
        }
        return ret;
    }
#endif
    fprintf(stderr, "postsrsd: unsupported endpoint '%s'\n", s);
    return -1;
}
