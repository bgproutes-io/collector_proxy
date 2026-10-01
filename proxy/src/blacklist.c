#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "../include/blacklist.h"

#define BLACKLIST_INITIAL_SIZE 8U
#define BLACKLIST_KEY_ASN      1U
#define BLACKLIST_KEY_IPV4     2U
#define BLACKLIST_KEY_IPV6     3U

typedef struct blacklist_key_s
{
    uint8_t type;
    uint8_t length;
    uint8_t bytes[16];
} Blacklist_key_t;

struct blacklist_entry_s
{
    uint32_t hash;
    Blacklist_key_t key;
    struct blacklist_entry_s *next;
};



static uint32_t Blacklist_hash(const Blacklist_key_t *key)
{
    /* FNV-1a is inexpensive and gives stable hashes for these tiny keys. */
    uint32_t hash = 2166136261U;
    hash = (hash ^ key->type) * 16777619U;
    for (uint8_t i = 0; i < key->length; i++)
        hash = (hash ^ key->bytes[i]) * 16777619U;
    return hash;
}



static bool Blacklist_key_equal(const Blacklist_key_t *a,
                                const Blacklist_key_t *b)
{
    return a->type == b->type && a->length == b->length &&
           memcmp(a->bytes, b->bytes, a->length) == 0;
}



static int Blacklist_key_from_asn(uint32_t asn, Blacklist_key_t *key)
{
    memset(key, 0, sizeof(*key));
    key->type = BLACKLIST_KEY_ASN;
    key->length = sizeof(asn);
    memcpy(key->bytes, &asn, sizeof(asn));
    return 0;
}



static int Blacklist_key_from_string(const char *ip, Blacklist_key_t *key)
{
    memset(key, 0, sizeof(*key));
    if (inet_pton(AF_INET, ip, key->bytes) == 1) {
        key->type = BLACKLIST_KEY_IPV4;
        key->length = 4;
        return 0;
    }
    if (inet_pton(AF_INET6, ip, key->bytes) == 1) {
        key->type = BLACKLIST_KEY_IPV6;
        key->length = 16;
        return 0;
    }
    return -1;
}



static int Blacklist_key_from_sockaddr(const SS *ip, Blacklist_key_t *key)
{
    memset(key, 0, sizeof(*key));
    if (ip->ss_family == AF_INET) {
        const struct sockaddr_in *ip4 = (const struct sockaddr_in *)ip;
        key->type = BLACKLIST_KEY_IPV4;
        key->length = 4;
        memcpy(key->bytes, &ip4->sin_addr, key->length);
        return 0;
    }
    if (ip->ss_family == AF_INET6) {
        const struct sockaddr_in6 *ip6 = (const struct sockaddr_in6 *)ip;
        key->type = BLACKLIST_KEY_IPV6;
        key->length = 16;
        memcpy(key->bytes, &ip6->sin6_addr, key->length);
        return 0;
    }
    return -1;
}



Blacklist_t *Blacklist_new(void)
{
    Blacklist_t *list = calloc(1, sizeof(*list));
    if (!list)
        return NULL;

    list->size = BLACKLIST_INITIAL_SIZE;
    list->buckets = calloc(list->size, sizeof(*list->buckets));
    if (!list->buckets) {
        free(list);
        return NULL;
    }
    return list;
}



void Blacklist_free(Blacklist_t *list)
{
    if (!list)
        return;
    for (size_t i = 0; i < list->size; i++) {
        Blacklist_entry_t *entry = list->buckets[i];
        while (entry) {
            Blacklist_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(list->buckets);
    free(list);
}



static int Blacklist_expand(Blacklist_t *list)
{
    size_t new_size = list->size * 2;
    Blacklist_entry_t **buckets = calloc(new_size, sizeof(*buckets));
    if (!buckets)
        return -1;

    for (size_t i = 0; i < list->size; i++) {
        Blacklist_entry_t *entry = list->buckets[i];
        while (entry) {
            Blacklist_entry_t *next = entry->next;
            size_t index = entry->hash & (new_size - 1);
            entry->next = buckets[index];
            buckets[index] = entry;
            entry = next;
        }
    }
    free(list->buckets);
    list->buckets = buckets;
    list->size = new_size;
    return 0;
}



static bool Blacklist_contains_key(const Blacklist_t *list,
                                   const Blacklist_key_t *key)
{
    if (!list || !list->count)
        return False;

    uint32_t hash = Blacklist_hash(key);
    size_t index = hash & (list->size - 1);
    for (Blacklist_entry_t *entry = list->buckets[index]; entry;
         entry = entry->next) {
        if (entry->hash == hash && Blacklist_key_equal(&entry->key, key))
            return True;
    }
    return False;
}



static int Blacklist_add_key(Blacklist_t *list, const Blacklist_key_t *key)
{
    if (!list)
        return -1;
    if (Blacklist_contains_key(list, key))
        return 0;
    if (list->count + 1 > list->size && Blacklist_expand(list) < 0)
        return -1;

    Blacklist_entry_t *entry = malloc(sizeof(*entry));
    if (!entry)
        return -1;
    entry->hash = Blacklist_hash(key);
    entry->key = *key;
    size_t index = entry->hash & (list->size - 1);
    entry->next = list->buckets[index];
    list->buckets[index] = entry;
    list->count++;
    return 0;
}



int Blacklist_add_asn(Blacklist_t *list, uint32_t asn)
{
    Blacklist_key_t key;
    Blacklist_key_from_asn(asn, &key);
    return Blacklist_add_key(list, &key);
}



int Blacklist_add_ip(Blacklist_t *list, const char *ip)
{
    Blacklist_key_t key;
    if (Blacklist_key_from_string(ip, &key) < 0)
        return -1;
    return Blacklist_add_key(list, &key);
}



bool Blacklist_contains_asn(const Blacklist_t *list, uint32_t asn)
{
    Blacklist_key_t key;
    Blacklist_key_from_asn(asn, &key);
    return Blacklist_contains_key(list, &key);
}



bool Blacklist_contains_ip(const Blacklist_t *list, const SS *ip)
{
    Blacklist_key_t key;
    if (!ip || Blacklist_key_from_sockaddr(ip, &key) < 0)
        return False;
    return Blacklist_contains_key(list, &key);
}
