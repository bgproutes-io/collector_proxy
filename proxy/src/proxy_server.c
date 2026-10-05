#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>

#include "../include/proxy_server.h"
#include "../include/debug.h"
#include "../include/my_tls.h"
#include "../include/timers.h"
#include "BMP_parse.h"

DECLARE_LLIST_CORE_FUNC(raw_message, Raw_message_t *)
Proxy_server_t* global_server;

static const char *protocol_name(Peering_protocol_t protocol)
{
    return protocol == PROTOCOL_BMP ? "BMP" : "BGP";
}

static void endpoint_string(const SS *address, char *dest, size_t size)
{
    if (sockaddr_to_string(address, dest, size) < 0 && dest[0] == '\0')
        snprintf(dest, size, "<unknown>");
}

static int collector_connect_failure(Proxy_server_t *proxy,
                                     const char *stage, int result)
{
    int saved_errno = errno ? errno : EIO;
    char endpoint[INET6_ADDRSTRLEN + 16] = {0};
    endpoint_string(&proxy->remote_addr, endpoint, sizeof(endpoint));
    proxy->consecutive_collector_connect_failures++;

    int level = proxy->consecutive_collector_connect_failures == 1 ||
                proxy->consecutive_collector_connect_failures % 6 == 0
        ? LOG_LEVEL_IMPORTANT : LOG_LEVEL_OPTIONAL;
    unsigned long tls_code = ERR_peek_last_error();
    if (tls_code) {
        char tls_detail[256];
        ERR_error_string_n(tls_code, tls_detail, sizeof(tls_detail));
        WARNING(level,
                "Collector connection failed during %s: endpoint=%s, "
                "tls=%s, attempt=%u, error=%s (%d), tls_error=%s; "
                "collector remains offline",
                stage, endpoint, proxy->use_ssl ? "enabled" : "disabled",
                proxy->consecutive_collector_connect_failures,
                strerror(saved_errno), saved_errno, tls_detail);
    } else {
        WARNING(level,
                "Collector connection failed during %s: endpoint=%s, "
                "tls=%s, attempt=%u, error=%s (%d); collector remains offline",
                stage, endpoint, proxy->use_ssl ? "enabled" : "disabled",
                proxy->consecutive_collector_connect_failures,
                strerror(saved_errno), saved_errno);
    }
    errno = saved_errno;
    return result;
}


static int reconnect_collector(void *arg)
{
    Proxy_server_t *proxy = arg;

    DEBUG(LOG_LEVEL_OPTIONAL,
          "Attempting scheduled collector reconnect; queued_messages=%u, "
          "queued_bytes=%llu, buffered_bmp_bytes=%u",
          llist_count(proxy->message_queue),
          (unsigned long long)proxy->queued_message_bytes,
          proxy->buffer.actLen);
    /* Periodical timers repeat on zero and are removed on nonzero. */
    return Proxy_server_connect(proxy) == 0 ? 1 : 0;
}


static void schedule_collector_reconnect(Proxy_server_t *proxy)
{
    if (!timers || Timer_job_lookup(timers, proxy, reconnect_collector))
    {
        return;
    }

    if (!Timer_list_add_tail(timers, reconnect_collector, proxy, 10,
                             TIMER_PERIODICAL))
    {
        ERROR(LOG_LEVEL_IMPORTANT,
              "Unable to schedule collector reconnect; automatic recovery "
              "is disabled");
    }
    else
    {
        INFO(LOG_LEVEL_IMPORTANT,
             "Collector reconnect scheduled every 10 seconds");
    }
}


static int64_t monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
        return -1;
    }

    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}


/* Return 0 when ready, 1 on timeout, and -1 on poll/clock errors. */
static int wait_for_socket(int sock, short events, int64_t deadline)
{
    for (;;)
    {
        int64_t now = monotonic_milliseconds();
        if (now < 0)
        {
            return -1;
        }

        if (now >= deadline)
        {
            return 1;
        }

        int64_t remaining = deadline - now;
        int timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
        struct pollfd descriptor = {
            .fd = sock,
            .events = events
        };

        int result = poll(&descriptor, 1, timeout);

        if (result > 0)
        {
            return 0;
        }

        if (result == 0)
        {
            return 1;
        }

        if (errno != EINTR)
        {
            return -1;
        }
    }
}



Proxy_server_t* Proxy_server_new(Config_t* cfg)
{
    if (!cfg)
    {
        errno = EINVAL;
        return NULL;
    }

    Proxy_server_t* proxy = calloc(1, sizeof(Proxy_server_t));
    if (!proxy)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to allocate proxy state: %s (%d)", strerror(errno), errno);
        return NULL;
    }

    proxy->listen_sock = -1;
    proxy->router_data_sock = -1;
    proxy->collector_data_sock = -1;
    proxy->listen_command_sock = -1;
    proxy->command_data_sock = -1;
    proxy->connect_timeout_ms = DEFAULT_PROXY_CONNECT_TIMEOUT_MS;
    proxy->queued_message_bytes = 0;
    proxy->message_queue = raw_message_llist_new(&Raw_message_free, NULL);

    if (!proxy->message_queue)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to allocate the offline message queue: %s (%d)",
              strerror(errno), errno);
        Proxy_server_free(proxy);
        return NULL;
    }

    proxy->cfg = cfg;

    /* If the client requested some SSL connection try to create the TLS
    context from the certificates provided in the configuration */
    if (cfg->use_tls)
    {
        proxy->ssl_ctx = create_tls_context(
            cfg->client_crt,
            cfg->client_key,
            cfg->ca_crt
        );

        if (!proxy->ssl_ctx)
        {
            ERROR(LOG_LEVEL_ALWAYS,
                  "Unable to initialize collector TLS using client_cert=%s, "
                  "client_key=%s, ca_cert=%s",
                  cfg->client_crt, cfg->client_key, cfg->ca_crt);
            Proxy_server_free(proxy);
            return NULL;
        }

        proxy->use_ssl = True;
    }

    proxy->proto = cfg->proto;

    if (proxy->proto == PROTOCOL_BGP)
    {
        proxy->use_bmp_filters = False;
    }
    else
    {
        if (cfg->blacklisted_asns->count + cfg->blacklisted_ips->count)
        {
            proxy->use_bmp_filters = True;
        }
        else
        {
            proxy->use_bmp_filters = False;
        }
    }

    /* Setup the network utils */
    memcpy(&proxy->remote_addr, &cfg->remote_addr, sizeof(SS));
    memcpy(&proxy->local_addr, &cfg->local_addr, sizeof(SS));

    sockaddr_set_port(&proxy->remote_addr, cfg->remote_port);
    sockaddr_set_port(&proxy->local_addr, cfg->local_port);

    char local_endpoint[INET6_ADDRSTRLEN + 16] = {0};
    char collector_endpoint[INET6_ADDRSTRLEN + 16] = {0};
    endpoint_string(&proxy->local_addr, local_endpoint, sizeof(local_endpoint));
    endpoint_string(&proxy->remote_addr, collector_endpoint,
                    sizeof(collector_endpoint));

    /* Create the socket that listens for connections from the router */
    proxy->listen_sock = socket(proxy->local_addr.ss_family, SOCK_STREAM, 0);
    if (proxy->listen_sock == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to create router listen socket for %s: %s (%d)",
              local_endpoint, strerror(errno), errno);
        Proxy_server_free(proxy);
        return NULL;
    }

    size_t addrLen = (proxy->local_addr.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    if (bind(proxy->listen_sock, (SA*)&proxy->local_addr, addrLen) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to bind router listen socket to %s: %s (%d)",
              local_endpoint, strerror(errno), errno);
        Proxy_server_free(proxy);
        return NULL;
    }

    if (listen(proxy->listen_sock, 3) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to listen for router connections on %s: %s (%d)",
              local_endpoint, strerror(errno), errno);
        Proxy_server_free(proxy);
        return NULL;
    }

    /* Create the socket for the commands */
    memcpy(&proxy->local_command_addr, &cfg->command_addr, sizeof(SS));

    sockaddr_set_port(&proxy->local_command_addr, cfg->command_port);

    char command_endpoint[INET6_ADDRSTRLEN + 16] = {0};
    endpoint_string(&proxy->local_command_addr, command_endpoint,
                    sizeof(command_endpoint));

    proxy->listen_command_sock = socket(proxy->local_command_addr.ss_family, SOCK_STREAM, 0);
    if (proxy->listen_command_sock == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to create command listen socket for %s: %s (%d)",
              command_endpoint, strerror(errno), errno);
        Proxy_server_free(proxy);
        return NULL;
    }

    addrLen = (proxy->local_command_addr.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    if (bind(proxy->listen_command_sock, (SA*)&proxy->local_command_addr, addrLen) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to bind command listen socket to %s: %s (%d)",
              command_endpoint, strerror(errno), errno);
        Proxy_server_free(proxy);
        return NULL;
    }

    if (listen(proxy->listen_command_sock, 3) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to listen for command connections on %s: %s (%d)",
              command_endpoint, strerror(errno), errno);
        Proxy_server_free(proxy);
        return NULL;
    }

    /* Try to connect the data soket to the remote collector */
    if (Proxy_server_connect(proxy) != 0)
    {
        schedule_collector_reconnect(proxy);
    }

    INFO(LOG_LEVEL_IMPORTANT,
         "Proxy ready: protocol=%s, router_listener=%s, collector=%s, "
         "tls=%s, bmp_filtering=%s, blacklisted_asns=%zu, "
         "blacklisted_ips=%zu, command_listener=%s",
         protocol_name(proxy->proto), local_endpoint, collector_endpoint,
         proxy->use_ssl ? "enabled" : "disabled",
         proxy->use_bmp_filters ? "enabled" : "disabled",
         cfg->blacklisted_asns ? cfg->blacklisted_asns->count : 0,
         cfg->blacklisted_ips ? cfg->blacklisted_ips->count : 0,
         command_endpoint);

    return proxy;
}



void Proxy_server_free(Proxy_server_t* proxy)
{
    if (!proxy)
    {
        return;
    }

    if (proxy->ssl)
    {
        SSL_shutdown(proxy->ssl);
        SSL_free(proxy->ssl);
    }
    if (proxy->collector_data_sock >= 0)
    {
        close(proxy->collector_data_sock);
    }
        
    if (proxy->router_data_sock >= 0)
    {
        close(proxy->router_data_sock);
    }
        
    if (proxy->listen_sock >= 0)
    {
        close(proxy->listen_sock);
    }
        
    if (proxy->command_data_sock >= 0)
    {
        close(proxy->command_data_sock);
    }
        
    if (proxy->listen_command_sock >= 0)
    {
        close(proxy->listen_command_sock);
    }

    Proxy_server_clear_message_queue(proxy);
    SSL_CTX_free(proxy->ssl_ctx);
    free(proxy);
}



void Raw_message_free(Raw_message_t *message)
{
    if (!message)
    {
        return;
    }

    free(message->data);
    free(message);
}



int Proxy_server_queue_message(Proxy_server_t *server,
                               const void *data, size_t length)
{
    if (!server || (!data && length) || length > MAX_BGP_MESSAGE_SIZE)
    {
        return -1;
    }

    if (!server->message_queue)
    {
        server->message_queue = raw_message_llist_new(&Raw_message_free, NULL);
        if (!server->message_queue)
        {
            return -1;
        }
    }

    Raw_message_t *message = calloc(1, sizeof(*message));
    if (!message)
    {
        return -1;
    }

    message->length = (uint32_t)length;
    if (length)
    {
        message->data = malloc(length);
        if (!message->data)
        {
            Raw_message_free(message);
            return -1;
        }
        memcpy(message->data, data, length);
    }

    if (!raw_message_llist_add_tail(server->message_queue, message, False))
    {
        Raw_message_free(message);
        return -1;
    }
    server->queued_message_bytes += length;
    return 0;
}



Raw_message_t *Proxy_server_dequeue_message(Proxy_server_t *server)
{
    if (!server || !server->message_queue)
    {
        return NULL;
    }

    Raw_message_t *message = raw_message_llist_pop_head(server->message_queue);
    if (message)
    {
        server->queued_message_bytes -= message->length;
    }
        
    return message;
}



void Proxy_server_clear_message_queue(Proxy_server_t *server)
{
    if (!server)
    {
        return;
    }

    raw_message_llist_free(server->message_queue);
    server->message_queue = NULL;
    server->queued_message_bytes = 0;
}



int Proxy_server_read(Proxy_server_t* proxy, void* buf, int buf_size)
{
    if (proxy->use_ssl)
    {
        ERR_clear_error();
        errno = 0;
        int size = SSL_read(proxy->ssl, buf, buf_size);

        if (size > 0)
        {
            return size;
        }

        int saved_errno = errno;
        int ssl_error = SSL_get_error(proxy->ssl, size);

        switch (ssl_error)
        {
            case SSL_ERROR_ZERO_RETURN:
                /* The peer sent a TLS close_notify alert. */
                return 0;

            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                errno = EAGAIN;
                return -1;

            case SSL_ERROR_SYSCALL:
                /* An EOF without close_notify is still a disconnected peer. */
                if (size == 0 && saved_errno == 0)
                {
                    return 0;
                }

                errno = saved_errno ? saved_errno : EIO;
                return -1;

            case SSL_ERROR_SSL:
                ERR_print_errors_fp(stderr);
                errno = EPROTO;
                return -1;

            default:
                errno = EIO;
                return -1;
        }
    }
    else
    {
        return recv(proxy->collector_data_sock, buf, buf_size, 0);
    }

    return -1;
}



int Proxy_server_send(Proxy_server_t* proxy, const void* buf, int size)
{
    if (!proxy || size < 0 || (!buf && size > 0))
    {
        errno = EINVAL;
        return -1;
    }

    int total = 0;
    while (total < size)
    {
        int written;
        if (proxy->use_ssl) 
        {
            ERR_clear_error();
            written = SSL_write(proxy->ssl, (const uint8_t *)buf + total, size - total);

            if (written <= 0)
            {
                int ssl_error = SSL_get_error(proxy->ssl, written);
                if (ssl_error == SSL_ERROR_SYSCALL && errno == EINTR)
                {
                    continue;
                }

                if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
                {
                    errno = EAGAIN;
                }
                else if (!errno)
                {
                    errno = ssl_error == SSL_ERROR_SSL ? EPROTO : EIO;
                }
                    
                return -1;
            }
        }
        else
        {
            written = send_no_sigpipe(proxy->collector_data_sock, (const uint8_t *)buf + total, size - total, 0);
            if (written < 0 && errno == EINTR)
            {
                continue;
            }

            if (written <= 0)
            {
                if (written == 0)
                {
                    errno = EPIPE;
                }
                return -1;
            }
        }
        total += written;
    }

    return total;
}



int Proxy_server_connect(Proxy_server_t *proxy)
{
    if (!proxy)
    {
        errno = EINVAL;
        ERROR(LOG_LEVEL_IMPORTANT,
              "Collector connection requested with a NULL proxy state");
        return -1;
    }

    if (proxy->collector_connected)
    {
        DEBUG(LOG_LEVEL_TOO_MUCH,
              "Collector connection request ignored: already connected");
        return 0;
    }

    if (proxy->remote_addr.ss_family != AF_INET &&
        proxy->remote_addr.ss_family != AF_INET6)
    {
        errno = EINVAL;
        return collector_connect_failure(proxy, "address validation", -1);
    }

    /* A descriptor without the connected state is inconsistent. */
    if (proxy->collector_data_sock >= 0)
    {
        errno = EALREADY;
        return collector_connect_failure(proxy, "socket state validation", -1);
    }

    uint32_t timeout_ms = proxy->connect_timeout_ms
        ? proxy->connect_timeout_ms
        : DEFAULT_PROXY_CONNECT_TIMEOUT_MS;
    int64_t started = monotonic_milliseconds();
    if (started < 0)
    {
        return collector_connect_failure(proxy, "monotonic clock read", -1);
    }

    int64_t deadline = started + timeout_ms;

    int sock = socket(proxy->remote_addr.ss_family, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return collector_connect_failure(proxy, "socket creation", -1);
    }

    int original_flags = fcntl(sock, F_GETFL, 0);
    if (original_flags < 0 || fcntl(sock, F_SETFL, original_flags | O_NONBLOCK) < 0)
    {
        int saved_errno = errno;
        close(sock);
        errno = saved_errno;
        return collector_connect_failure(proxy, "nonblocking socket setup", -1);
    }

    socklen_t addr_len = proxy->remote_addr.ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

    int connect_result = connect(sock, (SA *)&proxy->remote_addr, addr_len);
    if (connect_result < 0 && errno != EINPROGRESS)
    {
        int saved_errno = errno;
        close(sock);
        errno = saved_errno;
        return collector_connect_failure(proxy, "TCP connect", -1);
    }

    if (connect_result < 0)
    {
        int wait_result = wait_for_socket(sock, POLLOUT, deadline);
        if (wait_result != 0)
        {
            int saved_errno = wait_result == 1 ? ETIMEDOUT : errno;
            close(sock);
            errno = saved_errno;
            return collector_connect_failure(proxy, "TCP connect wait",
                                             wait_result);
        }

        int socket_error = 0;
        socklen_t error_length = sizeof(socket_error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) < 0 || socket_error)
        {
            int saved_errno = socket_error ? socket_error : errno;
            close(sock);
            errno = saved_errno;
            return collector_connect_failure(proxy, "TCP connect completion",
                                             -1);
        }
    }

    SSL *ssl = NULL;
    if (proxy->use_ssl)
    {
        if (!proxy->ssl_ctx)
        {
            close(sock);
            errno = EINVAL;
            return collector_connect_failure(proxy, "TLS context validation",
                                             -1);
        }

        ssl = SSL_new(proxy->ssl_ctx);
        if (!ssl || SSL_set_fd(ssl, sock) != 1)
        {
            if (ssl)
                SSL_free(ssl);
            close(sock);
            errno = EPROTO;
            return collector_connect_failure(proxy, "TLS session setup", -1);
        }

        for (;;)
        {
            ERR_clear_error();
            int result = SSL_connect(ssl);
            if (result == 1)
            {
                break;
            }

            int ssl_error = SSL_get_error(ssl, result);
            short events;
            if (ssl_error == SSL_ERROR_WANT_READ)
            {
                events = POLLIN;
            }
            else if (ssl_error == SSL_ERROR_WANT_WRITE)
            {
                events = POLLOUT;
            }
            else
            {
                SSL_free(ssl);
                close(sock);
                errno = EPROTO;
                return collector_connect_failure(proxy, "TLS handshake", -1);
            }

            int wait_result = wait_for_socket(sock, events, deadline);
            if (wait_result != 0)
            {
                int saved_errno = wait_result == 1 ? ETIMEDOUT : errno;
                SSL_free(ssl);
                close(sock);
                errno = saved_errno;
                return collector_connect_failure(proxy, "TLS handshake wait",
                                                 wait_result);
            }
        }

        if (SSL_get_verify_result(ssl) != X509_V_OK)
        {
            SSL_free(ssl);
            close(sock);
            errno = EPROTO;
            return collector_connect_failure(proxy,
                                             "TLS certificate verification",
                                             -1);
        }
    }

    if (fcntl(sock, F_SETFL, original_flags) < 0)
    {
        int saved_errno = errno;
        if (ssl)
        {
            SSL_free(ssl);
        }
            
        close(sock);
        errno = saved_errno;
        return collector_connect_failure(proxy, "blocking socket restore", -1);
    }

    proxy->collector_connected = True;
    proxy->collector_data_sock = sock;
    proxy->ssl = ssl;

    if (Proxy_server_empty_queued_messages(proxy) < 0)
    {
        return -1;
    }

    char endpoint[INET6_ADDRSTRLEN + 16] = {0};
    endpoint_string(&proxy->remote_addr, endpoint, sizeof(endpoint));
    INFO(LOG_LEVEL_IMPORTANT,
         "Collector connected: endpoint=%s, tls=%s, fd=%d, "
         "queued_messages=%u, queued_bytes=%llu",
         endpoint, proxy->use_ssl ? "enabled" : "disabled",
         proxy->collector_data_sock, llist_count(proxy->message_queue),
         (unsigned long long)proxy->queued_message_bytes);
    proxy->consecutive_collector_connect_failures = 0;

    return 0;
}



int Proxy_server_close_collector(Proxy_server_t* proxy)
{
    if (!proxy)
        return -1;

    char endpoint[INET6_ADDRSTRLEN + 16] = {0};
    endpoint_string(&proxy->remote_addr, endpoint, sizeof(endpoint));
    if (proxy->use_ssl)
    {
        if (proxy->ssl)
        {
            SSL_shutdown(proxy->ssl);
            SSL_free(proxy->ssl);
            proxy->ssl = NULL;
        }
    }

    if (proxy->collector_data_sock >= 0)
    {
        close(proxy->collector_data_sock);
    }

    proxy->collector_data_sock = -1;

    proxy->collector_connected = False;
    INFO(LOG_LEVEL_OPTIONAL,
         "Collector connection closed: endpoint=%s, queued_messages=%u, "
         "queued_bytes=%llu, buffered_bmp_bytes=%u",
         endpoint, llist_count(proxy->message_queue),
         (unsigned long long)proxy->queued_message_bytes,
         proxy->buffer.actLen);
    schedule_collector_reconnect(proxy);

    return 0;
}



static int Proxy_server_skip_openBMP_header(Proxy_server_t* proxy)
{
    int nbNewLine = 0;
    circBuf_t* buf = &proxy->buffer;

    if (buf->actLen < 4)
    {
        return 0;
    }

    if (CircBuf_get_8_without_reading(buf) == 'V')
    {
        /* Scan without consuming so a fragmented header remains intact. */
        for (uint32_t i = 0; i < buf->actLen; i++) {
            uint8_t byte = buf->buf[(buf->actIdxRead + i) % (MAX_BGP_MESSAGE_SIZE * 2)];
            if (byte == '\n')
            {
                nbNewLine++;
                if (nbNewLine == 2)
                {
                    CircBuf_forward_cursor(buf, i + 1);
                    return (int)i + 1;
                }
            }
            else
            {
                nbNewLine = 0;
            }
        }
        return -1;
    }

    uint8_t start_msg[4];
    CircBuf_get_without_reading(buf, start_msg, sizeof(start_msg));

    if (memcmp(start_msg, "OBMP", 4) != 0)
    {
        return 0;
    }
        

    if (buf->actLen < 8)
    {
        return -1;
    }

    uint8_t header_prefix[8];
    CircBuf_get_without_reading(buf, header_prefix, sizeof(header_prefix));
    uint16_t header_length = ((uint16_t)header_prefix[6] << 8) | header_prefix[7];

    if (header_length < sizeof(header_prefix))
    {
        CircBuf_forward_cursor(buf, 1);
        return -2;
    }
    if (buf->actLen < header_length)
    {
        return -1;
    }

    CircBuf_forward_cursor(buf, header_length);
    return header_length;
}



static BMP_parsed_msg_t* Proxy_server_read_one_message(
    Proxy_server_t* proxy, uint8_t* reason, uint8_t** msgBytes,
    uint32_t* msgSize)
{
    *msgBytes = NULL;
    int ret = Proxy_server_skip_openBMP_header(proxy);

    /* In case there is an OpenBMP header, but we do not have enough 
        data to read it entirely */
    if (ret == -1)
    {
        (*reason) = FAIL_READ_MSG_TOO_FEW_DATA;
        return NULL;
    }
    if (ret == -2)
    {
        (*reason) = FAIL_READ_MSG_PARSING_ERROR;
        return NULL;
    }

    circBuf_t* buf = &proxy->buffer;

    if (buf->actLen < 5)
    {
        (*reason) = FAIL_READ_MSG_TOO_FEW_DATA;
        return NULL;
    }

    uint8_t version  = CircBuf_read_8(buf);
    (*msgSize) = CircBuf_read_32(buf);
    CircBuf_backward_cursor(buf, 5);

    if ((*msgSize) < 6)
    {
        WARNING(LOG_LEVEL_OPTIONAL,
                "Malformed BMP header: version=%u, declared_length=%u, "
                "buffered_bytes=%u; advancing one byte to resynchronize",
                version, (*msgSize), buf->actLen);
        CircBuf_forward_cursor(buf, 1);
        (*reason) = FAIL_READ_MSG_PARSING_ERROR;
        return NULL;
    }

    /* We do not have enough data to read, skipping */
    if (buf->actLen < (*msgSize))
    {
        DEBUG(LOG_LEVEL_TOO_MUCH,
              "Waiting for fragmented BMP message: version=%u, "
              "buffered_bytes=%u, declared_length=%u, missing_bytes=%u",
              version, buf->actLen, (*msgSize), (*msgSize) - buf->actLen);
        if ((*msgSize) > MAX_BGP_MESSAGE_SIZE * 2)
        {
            WARNING(LOG_LEVEL_IMPORTANT,
                    "Rejected oversized BMP message: version=%u, "
                    "declared_length=%u, maximum=%u; advancing one byte "
                    "to resynchronize",
                    version, (*msgSize), MAX_BGP_MESSAGE_SIZE * 2);
            CircBuf_forward_cursor(buf, 1);
            (*reason) = FAIL_READ_MSG_PARSING_ERROR;
            return NULL;
        }
        (*reason) = FAIL_READ_MSG_TOO_FEW_DATA;
        return NULL;
    }

    /* Check version */
    if (version != 3)
    {
        /* Wrong version, skip message */
        WARNING(LOG_LEVEL_OPTIONAL,
                "Rejected BMP message with unsupported version=%u, "
                "length=%u; expected version=3",
                version, (*msgSize));
        CircBuf_forward_cursor(buf, (*msgSize));
        (*reason) = FAIL_READ_MSG_PARSING_ERROR;
        return NULL;
    }

    /* Get the full message in the form of a buffer */
    (*msgBytes) = calloc((*msgSize), sizeof(uint8_t));
    if (!(*msgBytes))
    {
        ERROR(LOG_LEVEL_IMPORTANT,
              "Unable to allocate %u bytes for a BMP message: %s (%d); "
              "discarding this message",
              (*msgSize), strerror(errno), errno);
        CircBuf_forward_cursor(buf, (*msgSize));
        (*reason) = FAIL_READ_MSG_PARSING_ERROR;
        return NULL;
    }

    CircBuf_read_n(buf, (*msgBytes), (*msgSize));
    (*reason) = MSG_PARSING_OK;
    BMP_parsed_msg_t* msg = BMP_parsed_msg_from_bytes((*msgBytes), (*msgSize));

    if (!msg)
    {
        (*reason) = FAIL_READ_MSG_PARSING_ERROR;
    }

    return msg;
}



#define FILTER_PROCESS_OK                 0
#define FILTER_PROCESS_COLLECTOR_ERROR   -1
#define FILTER_PROCESS_INPUT_OVERFLOW    -2

static int Proxy_server_process_bmp_stream(Proxy_server_t* proxy,
                                           const uint8_t* buf,
                                           int buf_len)
{
    circBuf_t* cbuf = &proxy->buffer;
    if (buf_len < 0 || (!buf && buf_len > 0)) 
    {
        errno = EINVAL;
        return FILTER_PROCESS_INPUT_OVERFLOW;
    }

    if ((uint32_t)buf_len > REMAINING_LEN(*cbuf)) 
    {
        ERROR(LOG_LEVEL_IMPORTANT,
              "BMP receive buffer overflow: buffered_bytes=%u, "
              "incoming_bytes=%d, capacity=%u; disconnecting router and "
              "discarding buffered data",
              cbuf->actLen, buf_len, MAX_BGP_MESSAGE_SIZE * 2);
        if (proxy->router_data_sock >= 0)
        {
            close(proxy->router_data_sock);
        }
            
        proxy->router_data_sock = -1;
        proxy->router_connected = False;
        proxy->consecutive_parsing_errors = 0;
        CircBuf_reset(cbuf);
        return FILTER_PROCESS_INPUT_OVERFLOW;
    }

    if (buf_len > 0)
    {
        CircBuf_write(cbuf, (uint8_t *)buf, (uint32_t)buf_len);
    }

    BMP_parsed_msg_t* msg = NULL;
    uint8_t reason = FAIL_READ_MSG_PARSING_ERROR;
    uint32_t msgSize;
    uint8_t* msgBytes = NULL;

    /* While there is at least one possible BMP message in the buffer */
    while (cbuf->actLen >= 5)
    {
        msg = Proxy_server_read_one_message(proxy, &reason, &msgBytes, &msgSize);

        /* In case we were not able to read the message */
        if (!msg)
        {
            /* Switch action according to the exit reason */
            if (reason == FAIL_READ_MSG_TOO_FEW_DATA)
            {
                return FILTER_PROCESS_OK;
            }
            else if (reason == FAIL_READ_MSG_PARSING_ERROR)
            {
                proxy->consecutive_parsing_errors++;
                WARNING(LOG_LEVEL_IMPORTANT,
                        "BMP parsing failed: consecutive_errors=%d/%d, "
                        "buffered_bytes=%u; attempting stream resynchronization",
                        proxy->consecutive_parsing_errors,
                        MAX_CONSECUTIVE_PARSING_ERRORS, cbuf->actLen);
            }
            else
            {
                proxy->consecutive_parsing_errors++;
                WARNING(LOG_LEVEL_IMPORTANT,
                        "BMP parsing stopped for unknown reason=%u: "
                        "consecutive_errors=%d/%d, buffered_bytes=%u",
                        reason, proxy->consecutive_parsing_errors,
                        MAX_CONSECUTIVE_PARSING_ERRORS, cbuf->actLen);
            }

            if (proxy->consecutive_parsing_errors >= MAX_CONSECUTIVE_PARSING_ERRORS)
            {
                ERROR(LOG_LEVEL_IMPORTANT,
                      "Disconnecting router after %d consecutive BMP parsing "
                      "errors; discarding %u buffered bytes",
                      proxy->consecutive_parsing_errors, cbuf->actLen);
                if (proxy->router_data_sock >= 0)
                    close(proxy->router_data_sock);
                CircBuf_reset(&proxy->buffer);
                proxy->router_data_sock = -1;
                proxy->router_connected = False;
                proxy->consecutive_parsing_errors = 0;
                return FILTER_PROCESS_INPUT_OVERFLOW;
            }

            free(msgBytes);
            msgBytes = NULL;
        }

        /* In case parsing was successful */
        else
        {
            proxy->consecutive_parsing_errors = 0;
            SS peer_address = {0};
            bool ip_blacklisted = False;
            if (msg->peer_addr_str[0] &&
                ip_to_sockaddr(msg->peer_addr_str, &peer_address, 0) == 0)
            {
                ip_blacklisted = Blacklist_contains_ip(
                    proxy->cfg->blacklisted_ips, &peer_address);
            }

            bool message_blacklisted = proxy->use_bmp_filters &&
                (Blacklist_contains_asn(proxy->cfg->blacklisted_asns,
                                        msg->peer_asn) ||
                 ip_blacklisted);

            if (!message_blacklisted)
            {
                if (Proxy_server_send(proxy, msgBytes, msgSize) == -1)
                {
                    int send_errno = errno;
                    /* Retain the complete message for the next connection. */
                    CircBuf_backward_cursor(cbuf, msgSize);
                    ERROR(LOG_LEVEL_IMPORTANT,
                          "Unable to forward filtered BMP message: "
                          "length=%u, peer_asn=%u, peer_address=%s, "
                          "error=%s (%d); message retained for reconnect",
                          msgSize, msg->peer_asn, msg->peer_addr_str,
                          strerror(send_errno), send_errno);
                    BMP_parsed_msg_free(msg);
                    free(msgBytes);
                    errno = send_errno;
                    Proxy_server_close_collector(proxy);
                    return FILTER_PROCESS_COLLECTOR_ERROR;
                }
            }
            else
            {
                INFO(LOG_LEVEL_OPTIONAL,
                     "Dropped blacklisted BMP message: length=%u, "
                     "peer_asn=%u, peer_address=%s",
                     msgSize, msg->peer_asn, msg->peer_addr_str);
            }
            
            free(msgBytes);
            msgBytes = NULL;
            BMP_parsed_msg_free(msg);
        }
    }

    return FILTER_PROCESS_OK;
}



int Proxy_server_empty_queued_messages(Proxy_server_t *proxy)
{
    if (!proxy)
    {
        return 0;
    }

    /* Always preserve BMP message boundaries.  Runtime blacklist commands may
     * toggle use_bmp_filters while a message is fragmented across TCP reads. */
    if (proxy->proto == PROTOCOL_BMP)
    {
        int status = Proxy_server_process_bmp_stream(proxy, NULL, 0);

        if (status == FILTER_PROCESS_COLLECTOR_ERROR)
        {
            return -1;
        }
            
        if (status == FILTER_PROCESS_INPUT_OVERFLOW)
        {
            Proxy_server_clear_message_queue(proxy);
            return 0;
        }
    }

    if (!proxy->message_queue)
    {
        return 0;
    }

    if (proxy->message_queue->count)
    {
        INFO(LOG_LEVEL_OPTIONAL,
             "Flushing offline queue: messages=%u, bytes=%llu, "
             "bmp_filtering=%s",
             proxy->message_queue->count,
             (unsigned long long)proxy->queued_message_bytes,
             proxy->use_bmp_filters ? "enabled" : "disabled");
    }

    while (proxy->collector_connected && proxy->message_queue->count)
    {
        Raw_message_t* message = llistnode_data(proxy->message_queue->head);
        
        if (proxy->proto == PROTOCOL_BMP)
        {
            int status = Proxy_server_process_bmp_stream(
                proxy, message->data, message->length);

            if (status == FILTER_PROCESS_INPUT_OVERFLOW)
            {
                ERROR(LOG_LEVEL_IMPORTANT,
                      "Discarding offline queue after malformed BMP stream: "
                      "messages=%u, bytes=%llu",
                      proxy->message_queue->count,
                      (unsigned long long)proxy->queued_message_bytes);
                Proxy_server_clear_message_queue(proxy);
                return 0;
            }

            message = Proxy_server_dequeue_message(proxy);
            Raw_message_free(message);

            if (status == FILTER_PROCESS_COLLECTOR_ERROR)
            {
                return -1;
            }
        }
        else
        {
            if (Proxy_server_send(proxy, message->data, message->length) == -1)
            {
                ERROR(LOG_LEVEL_IMPORTANT,
                      "Offline queue flush failed: message_length=%u, "
                      "remaining_messages=%u, remaining_bytes=%llu, "
                      "error=%s (%d); queue retained for reconnect",
                      message->length, proxy->message_queue->count,
                      (unsigned long long)proxy->queued_message_bytes,
                      strerror(errno), errno);
                Proxy_server_close_collector(proxy);
                return -1;
            }
            message = Proxy_server_dequeue_message(proxy);
            Raw_message_free(message);
        }
    }

    INFO(LOG_LEVEL_OPTIONAL, "Offline queue flush completed");

    return 0;
}



void Proxy_server_process_router_message(Proxy_server_t* proxy, uint8_t* buf, int buf_len)
{
    /* In case the collector is connected already, we can forward it */
    if (proxy->collector_connected)
    {
        /* BMP is always framed, even with an empty blacklist, so changing the
         * blacklist cannot switch parsing modes in the middle of a message. */
        if (proxy->proto == PROTOCOL_BMP)
        {
            Proxy_server_process_bmp_stream(proxy, buf, buf_len);
        }
        else
        {
            if (Proxy_server_send(proxy, buf, buf_len) == -1)
            {
                int send_errno = errno;
                ERROR(LOG_LEVEL_IMPORTANT,
                      "Collector forwarding failed: bytes=%d, error=%s (%d); "
                      "closing collector connection and queueing data",
                      buf_len, strerror(send_errno), send_errno);

                if (Proxy_server_queue_message(proxy, buf, buf_len) < 0)
                {
                    ERROR(LOG_LEVEL_ALWAYS,
                          "Data loss: unable to queue %d bytes after collector "
                          "send failure; queued_messages=%u, queued_bytes=%llu",
                          buf_len, llist_count(proxy->message_queue),
                          (unsigned long long)proxy->queued_message_bytes);
                }
        
                Proxy_server_close_collector(proxy);
                return;
            }
        }
    }
    else    /* Case where the collector is not connected yet */
    {
        if (Proxy_server_queue_message(proxy, buf, buf_len) < 0)
        {
            ERROR(LOG_LEVEL_ALWAYS,
                  "Data loss: unable to queue %d router bytes while collector "
                  "is offline; queued_messages=%u, queued_bytes=%llu",
                  buf_len, llist_count(proxy->message_queue),
                  (unsigned long long)proxy->queued_message_bytes);
        }
        else
        {
            DEBUG(LOG_LEVEL_TOO_MUCH,
                  "Queued router data while collector is offline: "
                  "chunk_bytes=%d, queued_messages=%u, queued_bytes=%llu",
                  buf_len, llist_count(proxy->message_queue),
                  (unsigned long long)proxy->queued_message_bytes);
        }
    }
}
