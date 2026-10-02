#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int main(void)
{
    test_plain_connect();
    test_tls_connect_timeout();

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
