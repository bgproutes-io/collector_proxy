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


static int reconnect_collector(void *arg)
{
    Proxy_server_t *proxy = arg;

    /* Periodical timers repeat on zero and are removed on nonzero. */
    return Proxy_server_connect(proxy) == 0 ? 1 : 0;
}


static void schedule_collector_reconnect(Proxy_server_t *proxy)
{
    if (!timers || Timer_job_lookup(timers, proxy, reconnect_collector))
        return;

    Timer_list_add_tail(timers, reconnect_collector, proxy, 10,
                        TIMER_PERIODICAL);
}


static int64_t monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}


/* Return 0 when ready, 1 on timeout, and -1 on poll/clock errors. */
static int wait_for_socket(int sock, short events, int64_t deadline)
{
    for (;;) {
        int64_t now = monotonic_milliseconds();
        if (now < 0)
            return -1;
        if (now >= deadline)
            return 1;

        int64_t remaining = deadline - now;
        int timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
        struct pollfd descriptor = {
            .fd = sock,
            .events = events
        };

        int result = poll(&descriptor, 1, timeout);
        if (result > 0)
            return 0;
        if (result == 0)
            return 1;
        if (errno != EINTR)
            return -1;
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
        ERROR(LOG_LEVEL_ALWAYS, "Unable to allocate memory for structure 'Proxy_server_t'.");
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
        ERROR(LOG_LEVEL_ALWAYS, "Unable to allocate memory for structure 'Proxy_server_t->message_queue'.");
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
            ERROR(LOG_LEVEL_ALWAYS, "Unable to create the TLS context for the proxy.");
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

    /* Create the socket that listens for connections from the router */
    proxy->listen_sock = socket(proxy->local_addr.ss_family, SOCK_STREAM, 0);
    if (proxy->listen_sock == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS, "Unable to create the listen TCP socket.");
        Proxy_server_free(proxy);
        return NULL;
    }

    size_t addrLen = (proxy->local_addr.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    if (bind(proxy->listen_sock, (SA*)&proxy->local_addr, addrLen) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS, "Unable to bind the listen socket.");
        Proxy_server_free(proxy);
        return NULL;
    }

    if (listen(proxy->listen_sock, 3) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS, "Unable to activate the listening of the incoming connection socket.");
        Proxy_server_free(proxy);
        return NULL;
    }

    /* Create the socket for the commands */
    memcpy(&proxy->local_command_addr, &cfg->command_addr, sizeof(SS));

    sockaddr_set_port(&proxy->local_command_addr, cfg->command_port);

    proxy->listen_command_sock = socket(proxy->local_command_addr.ss_family, SOCK_STREAM, 0);
    if (proxy->listen_command_sock == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS, "Unable to create the command listen TCP socket.");
        Proxy_server_free(proxy);
        return NULL;
    }

    addrLen = (proxy->local_command_addr.ss_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    if (bind(proxy->listen_command_sock, (SA*)&proxy->local_command_addr, addrLen) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS, "Unable to bind the command listen socket.");
        Proxy_server_free(proxy);
        return NULL;
    }

    if (listen(proxy->listen_command_sock, 3) == -1)
    {
        ERROR(LOG_LEVEL_ALWAYS, "Unable to activate the command listening of the incoming connection socket.");
        Proxy_server_free(proxy);
        return NULL;
    }

    /* Try to connect the data soket to the remote collector */
    if (Proxy_server_connect(proxy) != 0)
    {
        schedule_collector_reconnect(proxy);
    }

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
        close(proxy->collector_data_sock);
    if (proxy->router_data_sock >= 0)
        close(proxy->router_data_sock);
    if (proxy->listen_sock >= 0)
        close(proxy->listen_sock);
    if (proxy->command_data_sock >= 0)
        close(proxy->command_data_sock);
    if (proxy->listen_command_sock >= 0)
        close(proxy->listen_command_sock);

    Proxy_server_clear_message_queue(proxy);
    SSL_CTX_free(proxy->ssl_ctx);
    free(proxy);
}



void Raw_message_free(Raw_message_t *message)
{
    if (!message)
        return;
    free(message->data);
    free(message);
}



int Proxy_server_queue_message(Proxy_server_t *server,
                               const void *data, size_t length)
{
    if (!server || (!data && length) || length > MAX_BGP_MESSAGE_SIZE)
        return -1;

    if (!server->message_queue) {
        server->message_queue = raw_message_llist_new(&Raw_message_free, NULL);
        if (!server->message_queue)
            return -1;
    }

    Raw_message_t *message = calloc(1, sizeof(*message));
    if (!message)
        return -1;
    message->length = (uint32_t)length;
    if (length) {
        message->data = malloc(length);
        if (!message->data) {
            Raw_message_free(message);
            return -1;
        }
        memcpy(message->data, data, length);
    }

    if (!raw_message_llist_add_tail(server->message_queue, message, False)) {
        Raw_message_free(message);
        return -1;
    }
    server->queued_message_bytes += length;
    return 0;
}



Raw_message_t *Proxy_server_dequeue_message(Proxy_server_t *server)
{
    if (!server || !server->message_queue)
        return NULL;

    Raw_message_t *message = raw_message_llist_pop_head(server->message_queue);
    if (message)
        server->queued_message_bytes -= message->length;
    return message;
}



void Proxy_server_clear_message_queue(Proxy_server_t *server)
{
    if (!server)
        return;
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
    if (proxy->use_ssl)
    {
        return SSL_write(proxy->ssl, buf, size);
    }
    else
    {
        return send(proxy->collector_data_sock, buf, size, 0);
    }

    return -1;
}



int Proxy_server_connect(Proxy_server_t *proxy)
{
    if (!proxy)
    {
        errno = EINVAL;
        return -1;
    }

    if (proxy->collector_connected)
    {
        return 0;
    }

    if (proxy->remote_addr.ss_family != AF_INET &&
        proxy->remote_addr.ss_family != AF_INET6)
    {
        errno = EINVAL;
        return -1;
    }

    /* A descriptor without the connected state is inconsistent. */
    if (proxy->collector_data_sock >= 0)
    {
        errno = EALREADY;
        return -1;
    }

    uint32_t timeout_ms = proxy->connect_timeout_ms
        ? proxy->connect_timeout_ms
        : DEFAULT_PROXY_CONNECT_TIMEOUT_MS;
    int64_t started = monotonic_milliseconds();
    if (started < 0)
        return -1;
    int64_t deadline = started + timeout_ms;

    int sock = socket(proxy->remote_addr.ss_family, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    int original_flags = fcntl(sock, F_GETFL, 0);
    if (original_flags < 0 || fcntl(sock, F_SETFL, original_flags | O_NONBLOCK) < 0)
    {
        int saved_errno = errno;
        close(sock);
        errno = saved_errno;
        return -1;
    }

    socklen_t addr_len = proxy->remote_addr.ss_family == AF_INET
        ? sizeof(struct sockaddr_in)
        : sizeof(struct sockaddr_in6);

    int connect_result = connect(sock, (SA *)&proxy->remote_addr, addr_len);
    if (connect_result < 0 && errno != EINPROGRESS)
    {
        int saved_errno = errno;
        close(sock);
        errno = saved_errno;
        return -1;
    }

    if (connect_result < 0)
    {
        int wait_result = wait_for_socket(sock, POLLOUT, deadline);
        if (wait_result != 0)
        {
            int saved_errno = wait_result == 1 ? ETIMEDOUT : errno;
            close(sock);
            errno = saved_errno;
            return wait_result;
        }

        int socket_error = 0;
        socklen_t error_length = sizeof(socket_error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
                       &socket_error, &error_length) < 0 || socket_error)
        {
            int saved_errno = socket_error ? socket_error : errno;
            close(sock);
            errno = saved_errno;
            return -1;
        }
    }

    SSL *ssl = NULL;
    if (proxy->use_ssl)
    {
        if (!proxy->ssl_ctx)
        {
            close(sock);
            errno = EINVAL;
            return -1;
        }

        ssl = SSL_new(proxy->ssl_ctx);
        if (!ssl || SSL_set_fd(ssl, sock) != 1)
        {
            ERR_print_errors_fp(stderr);
            if (ssl)
                SSL_free(ssl);
            close(sock);
            errno = EPROTO;
            return -1;
        }

        for (;;) {
            ERR_clear_error();
            int result = SSL_connect(ssl);
            if (result == 1)
                break;

            int ssl_error = SSL_get_error(ssl, result);
            short events;
            if (ssl_error == SSL_ERROR_WANT_READ)
                events = POLLIN;
            else if (ssl_error == SSL_ERROR_WANT_WRITE)
                events = POLLOUT;
            else {
                ERR_print_errors_fp(stderr);
                SSL_free(ssl);
                close(sock);
                errno = EPROTO;
                return -1;
            }

            int wait_result = wait_for_socket(sock, events, deadline);
            if (wait_result != 0) {
                int saved_errno = wait_result == 1 ? ETIMEDOUT : errno;
                SSL_free(ssl);
                close(sock);
                errno = saved_errno;
                return wait_result;
            }
        }

        if (SSL_get_verify_result(ssl) != X509_V_OK) {
            SSL_free(ssl);
            close(sock);
            errno = EPROTO;
            return -1;
        }
    }

    if (fcntl(sock, F_SETFL, original_flags) < 0)
    {
        int saved_errno = errno;
        if (ssl)
            SSL_free(ssl);
        close(sock);
        errno = saved_errno;
        return -1;
    }

    proxy->collector_connected = True;
    proxy->collector_data_sock = sock;
    proxy->ssl = ssl;

    Proxy_server_empty_queued_messages(proxy);

    return 0;
}



int Proxy_server_close_collector(Proxy_server_t* proxy)
{
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
        close(proxy->collector_data_sock);
    proxy->collector_data_sock = -1;

    proxy->collector_connected = False;
    schedule_collector_reconnect(proxy);

    return 0;
}



int Proxy_server_skip_openBMP_header(Proxy_server_t* proxy)
{
    int nbNewLine = 0, nread = 0;
    circBuf_t* buf = &proxy->buffer;
    uint8_t ver_maj, ver_min;
    uint16_t u16;

    /* Skip this header if we do not have enough to read */
    if (buf->actLen < 4)
    {
        return 0;
    }

    /* If we have an OpenBMP ASCII header */
    if (CircBuf_get_8_without_reading(buf) == 'V')
    {
        /* Iterate until we find double new line (i.e., end of OpenBMP 
            ASCII header) */
        while (buf->actLen > 0)
        {
            if (nbNewLine == 2)
            {
                return nread;
            }
            
            /* Do not really read in case there is not enough data in the
                circular buffer for full message */
            if (CircBuf_read_8(buf) == '\n')
            {
                nbNewLine++;
            }
            else
            {
                nbNewLine = 0;
            }
            nread++;
        }

        CircBuf_backward_cursor(buf, nread);
        return -1;
    }

    /* Get the first 4 bytes to see if we have  */
    char start_msg[5];
    memset(start_msg, 0, 5);
    CircBuf_get_without_reading(buf, (uint8_t*)start_msg, 4);

    if (memcmp(start_msg, "OBMP", 4) != 0)
    {
        /* In this case, we consider that this is not an OpenBMP message,
            but rather a regular one */
        return 0;
    }
    CircBuf_forward_cursor(buf, 4);
    nread += 4;

    /* Get version information */
    ver_maj = CircBuf_read_8(buf);
    ver_min = CircBuf_read_8(buf);
    nread += 2;

    UNUSED(ver_maj);
    UNUSED(ver_min);

    /* Check if we have enough to read, at least until the header length */
    u16 = CircBuf_read_16(buf);
    nread += 2;

    if ((uint32_t)(u16 - nread) > buf->actLen)
    {
        /* In this case, we do not have enough data to read full OpenBMP header */
        CircBuf_backward_cursor(buf, nread);
        return -1;
    }

    /* For now ignore the header, no useful information inside */
    CircBuf_forward_cursor(buf, u16 - nread);
    nread = u16;

    return nread;
}



BMP_parsed_msg_t* Proxy_server_read_one_message(Proxy_server_t* proxy, uint8_t* reason, char** msgBytes, uint32_t* msgSize)
{
    int ret = Proxy_server_skip_openBMP_header(proxy);

    /* In case there is an OpenBMP header, but we do not have enough 
        data to read it entirely */
    if (ret == -1)
    {
        (*reason) = FAIL_READ_MSG_TOO_FEW_DATA;
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
        WARNING(LOG_LEVEL_IMPORTANT, "We received a BMP message with an invalid length '%u'. Version read is %d.", (*msgSize), version);
        CircBuf_forward_cursor(buf, 1);
        (*reason) = FAIL_READ_MSG_PARSING_ERROR;
        return NULL;
    }

    /* We do not have enough data to read, skipping */
    if (buf->actLen < (*msgSize))
    {
        WARNING(LOG_LEVEL_TOO_MUCH, "We do not have enough to read %u vs %u", buf->actLen, (*msgSize));
        if ((*msgSize) > MAX_BGP_MESSAGE_SIZE * 2)
        {
            WARNING(LOG_LEVEL_IMPORTANT, "We received a surprisingly long message of size '%u'. Version read is %d.", (*msgSize), version);
            CircBuf_reset(buf);
            // exit(1);
        }
        (*reason) = FAIL_READ_MSG_TOO_FEW_DATA;
        return NULL;
    }

    /* Check version */
    if (version != 3)
    {
        /* Wrong version, skip message */
        WARNING(LOG_LEVEL_TOO_MUCH, "Version is not correct");
        CircBuf_forward_cursor(buf, (*msgSize));
        (*reason) = FAIL_READ_MSG_PARSING_ERROR;
        return NULL;
    }

    /* Get the full message in the form of a buffer */
    (*msgBytes) = calloc((*msgSize), sizeof(uint8_t));
    if (!(*msgBytes))
    {
        ERROR(LOG_LEVEL_IMPORTANT, "Unable to allocate BMP message buffer of size %u.", (*msgSize));
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



void Proxy_server_process_filtered_message(Proxy_server_t* proxy, uint8_t* buf, int buf_len)
{
    circBuf_t* cbuf = &proxy->buffer;
    CircBuf_write(cbuf, buf, buf_len);

    BMP_parsed_msg_t* msg = NULL;
    uint8_t reason;
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
                return;
            }
            else if (reason == FAIL_READ_MSG_PARSING_ERROR)
            {
                WARNING(LOG_LEVEL_ALWAYS, "Failed to parse the BMP message.");
                proxy->consecutive_parsing_errors++;
            }
            else
            {
                WARNING(LOG_LEVEL_IMPORTANT, "Cannot parse message for another reason.");
                proxy->consecutive_parsing_errors++;
            }

            if (proxy->consecutive_parsing_errors >= MAX_CONSECUTIVE_PARSING_ERRORS)
            {
                close(proxy->router_data_sock);
                CircBuf_reset(&proxy->buffer);
                proxy->router_data_sock = -1;
                proxy->router_connected = False;
                return;
            }

            free(msgBytes);
        }

        /* In case parsing was successful */
        else
        {
            if (!Blacklist_contains_asn(proxy->cfg->blacklisted_asns, msg->peer_asn) && 
                !Blacklist_contains_ip(proxy->cfg->blacklisted_ips, msg->peer_addr))
            {
                if (Proxy_server_send(proxy, msgBytes, msgSize) == -1)
                {
                    BMP_parsed_msg_free(msg);
                    free(msgBytes);
                    ERROR(LOG_LEVEL_IMPORTANT, "Error when sending message from queue to the collector.");
                    Proxy_server_close_collector(proxy);
                    return;
                }
            }
            
            free(msgBytes);
            BMP_parsed_msg_free(msg);
        }
    }
}



/* Queue flushing is intentionally outside this unit test's scope. */
void Proxy_server_empty_queued_messages(Proxy_server_t *proxy)
{
    Raw_message_t* message = NULL;
    while (proxy->message_queue->count)
    {
        message = Proxy_server_dequeue_message(proxy);
        
        if (proxy->use_bmp_filters)
        {
            Proxy_server_process_filtered_message(proxy, message->data, message->length);
        }
        else
        {
            if (Proxy_server_send(proxy, message->data, message->length) == -1)
            {
                ERROR(LOG_LEVEL_IMPORTANT, "Error when sending message from queue to the collector.");
                Raw_message_free(message);
                Proxy_server_close_collector(proxy);
                return;
            }
        }

        Raw_message_free(message);
    }
}



void Proxy_server_process_router_message(Proxy_server_t* proxy, uint8_t* buf, int buf_len)
{
    /* In case the collector is connected already, we can forward it */
    if (proxy->collector_connected)
    {
        /* In this case we are using BMP and some filters, need more processing */
        if (proxy->use_bmp_filters)
        {
            Proxy_server_process_filtered_message(proxy, buf, buf_len);
        }
        else
        {
            if (Proxy_server_send(proxy, buf, buf_len) == -1)
            {
                ERROR(LOG_LEVEL_IMPORTANT, "Error when forwarding message to the collector.");
                Proxy_server_close_collector(proxy);
                return;
            }
        }
    }
    else    /* Case where the collector is not connected yet */
    {
        Proxy_server_queue_message(proxy, buf, buf_len);
    }
}
