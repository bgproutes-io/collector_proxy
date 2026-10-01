#ifndef __MY_TLS_H__
#define __MY_TLS_H__

#include "common.h"

SSL_CTX *create_tls_context(const char *client_crt,
                            const char *client_key,
                            const char *ca_crt);

#endif
