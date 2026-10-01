#ifndef __BLACKLIST_H__
#define __BLACKLIST_H__

#include "common.h"
#include "utils.h"

/*
 * Read-optimised hash sets used by the proxy configuration.  This is a
 * small, self-contained adaptation of the collector's set structure: keys
 * are stored by value, buckets use separate chaining, and the table grows
 * before the load factor exceeds one.
 */
typedef struct blacklist_entry_s Blacklist_entry_t;

typedef struct blacklist_s
{
    Blacklist_entry_t **buckets;
    size_t size;
    size_t count;
} Blacklist_t;

Blacklist_t *Blacklist_new(void);
void Blacklist_free(Blacklist_t *list);

int Blacklist_add_asn(Blacklist_t *list, uint32_t asn);
int Blacklist_add_ip(Blacklist_t *list, const char *ip);

bool Blacklist_contains_asn(const Blacklist_t *list, uint32_t asn);
bool Blacklist_contains_ip(const Blacklist_t *list, const SS *ip);

#endif
