#include "../include/my_tls.h"


SSL_CTX *create_tls_context(
    const char *client_crt,
    const char *client_key,
    const char *ca_crt
)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    /* Client identity used for mutual TLS. */
    if (SSL_CTX_use_certificate_file(
            ctx, client_crt, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (SSL_CTX_use_PrivateKey_file(
            ctx, client_key, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (SSL_CTX_check_private_key(ctx) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    /* CA used to verify the collector certificate. */
    if (SSL_CTX_load_verify_locations(
            ctx, ca_crt, NULL) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    return ctx;
}
