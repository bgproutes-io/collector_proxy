#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "proxy_server.h"





static void test_plain_connect(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        assert(errno == EACCES || errno == EPERM || errno == EAFNOSUPPORT);
        return;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(listener, (SA *)&address, sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);

    socklen_t address_len = sizeof(address);
    assert(getsockname(listener, (SA *)&address, &address_len) == 0);

    Proxy_server_t server = {0};
    server.collector_data_sock = -1;
    memcpy(&server.remote_addr, &address, sizeof(address));
    assert(Proxy_server_connect(&server) == 0);
    assert(server.collector_data_sock >= 0);
    assert(server.ssl == NULL);
    assert(Proxy_server_connect(&server) == 0);

    int accepted = accept(listener, NULL, NULL);
    assert(accepted >= 0);
    close(accepted);
    close(server.collector_data_sock);
    close(listener);
}

static void test_tls_connect_timeout(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        assert(errno == EACCES || errno == EPERM || errno == EAFNOSUPPORT);
        return;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (SA *)&address, sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    socklen_t address_len = sizeof(address);
    assert(getsockname(listener, (SA *)&address, &address_len) == 0);

    Proxy_server_t server = {0};
    server.collector_data_sock = -1;
    server.connect_timeout_ms = 25;
    server.use_ssl = True;
    server.ssl_ctx = SSL_CTX_new(TLS_client_method());
    assert(server.ssl_ctx);
    SSL_CTX_set_verify(server.ssl_ctx, SSL_VERIFY_NONE, NULL);
    memcpy(&server.remote_addr, &address, sizeof(address));

    errno = 0;
    assert(Proxy_server_connect(&server) == 1);
    assert(errno == ETIMEDOUT);
    assert(server.collector_data_sock == -1);
    assert(server.ssl == NULL);

    SSL_CTX_free(server.ssl_ctx);
    close(listener);
}

static int make_tcp_pair(int sockets[2])
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return -1;

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (SA *)&address, sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    socklen_t address_len = sizeof(address);
    assert(getsockname(listener, (SA *)&address, &address_len) == 0);

    sockets[0] = socket(AF_INET, SOCK_STREAM, 0);
    assert(sockets[0] >= 0);
    assert(connect(sockets[0], (SA *)&address, sizeof(address)) == 0);
    sockets[1] = accept(listener, NULL, NULL);
    assert(sockets[1] >= 0);
    close(listener);
    return 0;
}

static void close_with_reset(int socket_fd)
{
    struct linger linger = {.l_onoff = 1, .l_linger = 0};
    assert(setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, &linger,
                      sizeof(linger)) == 0);
    close(socket_fd);
}

static void test_empty_queue_sends_everything(void)
{
    int sockets[2];
    if (make_tcp_pair(sockets) < 0)
        return;

    Proxy_server_t server = {0};
    server.collector_connected = True;
    server.collector_data_sock = sockets[0];
    const uint8_t first[] = {1, 2, 3};
    const uint8_t second[] = {4, 5};
    uint8_t received[sizeof(first) + sizeof(second)] = {0};

    assert(Proxy_server_queue_message(&server, first, sizeof(first)) == 0);
    assert(Proxy_server_queue_message(&server, second, sizeof(second)) == 0);
    assert(Proxy_server_empty_queued_messages(&server) == 0);
    assert(llist_count(server.message_queue) == 0);
    assert(server.queued_message_bytes == 0);
    assert(recv(sockets[1], received, sizeof(received), MSG_WAITALL) ==
           (ssize_t)sizeof(received));
    assert(memcmp(received, "\x01\x02\x03\x04\x05", sizeof(received)) == 0);

    Proxy_server_clear_message_queue(&server);
    close(sockets[0]);
    close_with_reset(sockets[1]);
}

static void test_failed_flush_retains_message(void)
{
    int sockets[2];
    if (make_tcp_pair(sockets) < 0)
        return;

    Proxy_server_t server = {0};
    server.collector_connected = True;
    server.collector_data_sock = sockets[0];
    const uint8_t message[] = {1, 2, 3};
    assert(Proxy_server_queue_message(&server, message, sizeof(message)) == 0);
    close_with_reset(sockets[1]);

    assert(Proxy_server_empty_queued_messages(&server) == -1);
    assert(server.collector_connected == False);
    assert(llist_count(server.message_queue) == 1);
    assert(server.queued_message_bytes == sizeof(message));

    Proxy_server_clear_message_queue(&server);
}

static void test_fragmented_openbmp_header(void)
{
    Proxy_server_t server = {0};
    server.proto = PROTOCOL_BMP;
    server.collector_connected = True;
    server.collector_data_sock = -1;
    server.use_bmp_filters = True;
    const uint8_t first[] = {'O', 'B', 'M', 'P', 1};
    const uint8_t second[] = {0, 0, 8};

    Proxy_server_process_router_message(&server, (uint8_t *)first,
                                        sizeof(first));
    assert(server.buffer.actLen == sizeof(first));
    assert(server.buffer.actIdxRead == 0);

    Proxy_server_process_router_message(&server, (uint8_t *)second,
                                        sizeof(second));
    assert(server.buffer.actLen == 0);
}

static void test_filtered_input_overflow_disconnects_router(void)
{
    Proxy_server_t server = {0};
    uint8_t bytes[] = {1, 2};
    server.proto = PROTOCOL_BMP;
    server.collector_connected = True;
    server.collector_data_sock = -1;
    server.router_connected = True;
    server.router_data_sock = -1;
    server.use_bmp_filters = True;
    server.buffer.actLen = MAX_BGP_MESSAGE_SIZE * 2 - 1;

    Proxy_server_process_router_message(&server, bytes, sizeof(bytes));
    assert(server.router_connected == False);
    assert(server.router_data_sock == -1);
    assert(server.buffer.actLen == 0);
}

static void test_filter_toggle_preserves_fragmented_bmp_message(void)
{
    int sockets[2];
    if (make_tcp_pair(sockets) < 0)
        return;

    Config_t config = {0};
    config.blacklisted_asns = Blacklist_new();
    config.blacklisted_ips = Blacklist_new();
    assert(config.blacklisted_asns);
    assert(config.blacklisted_ips);

    Proxy_server_t server = {0};
    server.proto = PROTOCOL_BMP;
    server.cfg = &config;
    server.collector_connected = True;
    server.collector_data_sock = sockets[0];

    /* A minimal valid BMP initiation message. */
    const uint8_t message[] = {3, 0, 0, 0, 6, 4};
    uint8_t received[sizeof(message)] = {0};

    server.use_bmp_filters = False;
    Proxy_server_process_router_message(&server, (uint8_t *)message, 3);
    assert(server.buffer.actLen == 3);

    server.use_bmp_filters = True;
    Proxy_server_process_router_message(&server, (uint8_t *)message + 3, 3);
    assert(server.buffer.actLen == 0);
    assert(recv(sockets[1], received, sizeof(received), MSG_WAITALL) ==
           (ssize_t)sizeof(received));
    assert(memcmp(received, message, sizeof(message)) == 0);

    memset(received, 0, sizeof(received));
    Proxy_server_process_router_message(&server, (uint8_t *)message, 3);
    assert(server.buffer.actLen == 3);

    server.use_bmp_filters = False;
    Proxy_server_process_router_message(&server, (uint8_t *)message + 3, 3);
    assert(server.buffer.actLen == 0);
    assert(recv(sockets[1], received, sizeof(received), MSG_WAITALL) ==
           (ssize_t)sizeof(received));
    assert(memcmp(received, message, sizeof(message)) == 0);

    Blacklist_free(config.blacklisted_asns);
    Blacklist_free(config.blacklisted_ips);
    close(sockets[0]);
    close_with_reset(sockets[1]);
}

static void test_direct_send_failure_queues_message(void)
{
    int sockets[2];
    if (make_tcp_pair(sockets) < 0)
        return;

    Proxy_server_t server = {0};
    server.collector_connected = True;
    server.collector_data_sock = sockets[0];
    const uint8_t message[] = {1, 2, 3};
    close_with_reset(sockets[1]);

    Proxy_server_process_router_message(&server, (uint8_t *)message,
                                        sizeof(message));
    assert(server.collector_connected == False);
    assert(llist_count(server.message_queue) == 1);
    assert(server.queued_message_bytes == sizeof(message));

    Proxy_server_clear_message_queue(&server);
}

int main(void)
{
    test_plain_connect();
    test_tls_connect_timeout();
    test_empty_queue_sends_everything();
    test_failed_flush_retains_message();
    test_direct_send_failure_queues_message();
    test_fragmented_openbmp_header();
    test_filtered_input_overflow_disconnects_router();
    test_filter_toggle_preserves_fragmented_bmp_message();

    Proxy_server_t server = {0};
    const uint8_t first[] = {1, 2, 3, 4};

    assert(Proxy_server_queue_message(&server, first, sizeof(first)) == 0);
    assert(Proxy_server_queue_message(&server, NULL, 0) == 0);

    uint8_t *largest = malloc(MAX_BGP_MESSAGE_SIZE);
    assert(largest);
    memset(largest, 0xa5, MAX_BGP_MESSAGE_SIZE);
    assert(Proxy_server_queue_message(&server, largest,
                                      MAX_BGP_MESSAGE_SIZE) == 0);
    assert(Proxy_server_queue_message(&server, largest,
                                      MAX_BGP_MESSAGE_SIZE + 1U) == -1);
    free(largest);

    assert(llist_count(server.message_queue) == 3);
    assert(server.queued_message_bytes ==
           sizeof(first) + MAX_BGP_MESSAGE_SIZE);

    Raw_message_t *message = Proxy_server_dequeue_message(&server);
    assert(message);
    assert(message->length == sizeof(first));
    assert(memcmp(message->data, first, sizeof(first)) == 0);
    Raw_message_free(message);

    message = Proxy_server_dequeue_message(&server);
    assert(message);
    assert(message->length == 0);
    Raw_message_free(message);

    message = Proxy_server_dequeue_message(&server);
    assert(message);
    assert(message->length == MAX_BGP_MESSAGE_SIZE);
    assert(message->data[0] == 0xa5);
    assert(message->data[MAX_BGP_MESSAGE_SIZE - 1] == 0xa5);
    Raw_message_free(message);

    assert(Proxy_server_dequeue_message(&server) == NULL);
    assert(llist_count(server.message_queue) == 0);
    assert(server.queued_message_bytes == 0);

    assert(Proxy_server_queue_message(&server, first, sizeof(first)) == 0);
    Proxy_server_clear_message_queue(&server);
    assert(server.message_queue == NULL);
    assert(server.queued_message_bytes == 0);
    return 0;
}
