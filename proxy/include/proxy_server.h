#ifndef __PROXY_SERVER_H__
#define __PROXY_SERVER_H__

#include "common.h"
#include "utils.h"
#include "config.h"
#include "llist.h"

#define DEFAULT_PROXY_CONNECT_TIMEOUT_MS 1000U


typedef struct raw_message_s
{
    uint32_t length;
    uint8_t* data;
}
Raw_message_t;

DECLARE_LLIST_STRUCTURES(raw_message, Raw_message_t *)
DECLARE_LLIST_PROTOTYPES(raw_message, Raw_message_t *)


typedef struct proxy_server_s
{
    /* General information */
    Peering_protocol_t proto;       /* Type of protocol used by this client */
    bool use_bmp_filters;           /* Can we just forward the received data, or do
                                        we need to filter some portions of it */
    bool collector_connected;
    bool router_connected;

    /* TCP information */
    int listen_sock;
    int router_data_sock;
    int collector_data_sock;
    SS remote_addr;
    SS local_addr;
    uint32_t connect_timeout_ms;

    /* TLS information */
    SSL_CTX *ssl_ctx;
    SSL *ssl;
    bool use_ssl;
    Config_t* cfg;

    /* BMP-only information */
    raw_message_llist_t *message_queue;
    uint64_t queued_message_bytes;

}
Proxy_server_t;


Proxy_server_t* Proxy_server_new(Config_t* cfg);
void Proxy_server_free(Proxy_server_t* proxy);

/**
 * Connect to the configured remote collector and, when enabled, complete the
 * client-side TLS handshake.
 *
 * @return 0 on success, 1 when the connection deadline expires, and -1 for
 *         any other failure.
 */
int Proxy_server_connect(Proxy_server_t *proxy);


/** Copy one raw message into the tail of the server FIFO. */
int Proxy_server_queue_message(Proxy_server_t *server,
                               const void *data, size_t length);

/** Remove the oldest queued message. The caller owns the returned message. */
Raw_message_t *Proxy_server_dequeue_message(Proxy_server_t *server);

/** Free a dequeued message. */
void Raw_message_free(Raw_message_t *message);

/** Free all messages and queue bookkeeping owned by a server. */
void Proxy_server_clear_message_queue(Proxy_server_t *server);


#endif
