#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

static void write_config(char path[], const char *extra)
{
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fprintf(file,
            "local_addr: 127.0.0.1\n"
            "local_port: 5000\n"
            "collector_ip: 2001:db8::1\n"
            "collector_port: 5001\n"
            "%s", extra);
    assert(fclose(file) == 0);
}

int main(void)
{
    char valid_path[] = "/tmp/proxy-config-valid-XXXXXX";
    write_config(valid_path,
                 "blacklisted_asns: 64512, 64513,64512\n"
                 "blacklisted_ips: 192.0.2.1, 2001:db8::feed\n"
                 "proto: BmP\n");

    assert(Config_read(valid_path) == 0);
    assert(config.debug_level == DEFAULT_DEBUG_LEVEL);
    assert(config.proto_configured);
    assert(config.proto == PROTOCOL_BMP);
    assert(config.blacklisted_asns->count == 2);
    assert(config.blacklisted_ips->count == 2);
    assert(Config_is_asn_blacklisted(64512));
    assert(!Config_is_asn_blacklisted(64514));

    SS ip;
    assert(ip_to_sockaddr("192.0.2.1", &ip, 1234) == 0);
    assert(Config_is_ip_blacklisted(&ip));
    assert(ip_to_sockaddr("2001:db8::feed", &ip, 1234) == 0);
    assert(Config_is_ip_blacklisted(&ip));
    char endpoint[80];
    assert(sockaddr_to_string(&ip, endpoint, sizeof(endpoint)) == 0);
    assert(strcmp(endpoint, "[2001:db8::feed]:1234") == 0);
    assert(ip_to_sockaddr("192.0.2.2", &ip, 0) == 0);
    assert(sockaddr_to_string(&ip, endpoint, sizeof(endpoint)) == 0);
    assert(strcmp(endpoint, "192.0.2.2:0") == 0);
    assert(!Config_is_ip_blacklisted(&ip));
    unlink(valid_path);

    char legacy_path[] = "/tmp/proxy-config-legacy-XXXXXX";
    write_config(legacy_path,
                 "blaclisted_ips: 198.51.100.8\n"
                 "blacklisted_asns:\n"
                 "proto: bgp\n");
    assert(Config_read(legacy_path) == 0);
    assert(config.proto == PROTOCOL_BGP);
    assert(config.blacklisted_asns->count == 0);
    assert(ip_to_sockaddr("198.51.100.8", &ip, 0) == 0);
    assert(Config_is_ip_blacklisted(&ip));
    unlink(legacy_path);

    char invalid_path[] = "/tmp/proxy-config-invalid-XXXXXX";
    write_config(invalid_path,
                 "blacklisted_asns: 64512,not-an-asn\n"
                 "proto: bmp\n");
    assert(Config_read(invalid_path) == -1);
    assert(config.blacklisted_asns == NULL);
    assert(config.blacklisted_ips == NULL);
    unlink(invalid_path);

    Config_cleanup();

    Blacklist_t *list = Blacklist_new();
    assert(list);

    for (uint32_t asn = 64512; asn < 64576; asn++)
        assert(Blacklist_add_asn(list, asn) == 0);
    assert(list->count == 64);
    assert(Blacklist_del_asn(list, 64544) == 0);
    assert(!Blacklist_contains_asn(list, 64544));
    assert(list->count == 63);

    /* Deleting a missing entry is idempotent and does not change the count. */
    assert(Blacklist_del_asn(list, 64544) == 0);
    assert(list->count == 63);

    assert(Blacklist_add_ip(list, "192.0.2.10") == 0);
    assert(Blacklist_add_ip(list, "2001:db8::10") == 0);
    assert(list->count == 65);
    assert(Blacklist_del_ip(list, "192.0.2.10") == 0);
    assert(ip_to_sockaddr("192.0.2.10", &ip, 0) == 0);
    assert(!Blacklist_contains_ip(list, &ip));
    assert(Blacklist_del_ip(list, "2001:db8::10") == 0);
    assert(ip_to_sockaddr("2001:db8::10", &ip, 0) == 0);
    assert(!Blacklist_contains_ip(list, &ip));
    assert(list->count == 63);

    assert(Blacklist_del_ip(list, "not-an-ip") == -1);
    assert(Blacklist_del_ip(list, NULL) == -1);
    assert(Blacklist_del_asn(NULL, 64512) == -1);
    Blacklist_free(list);
    return 0;
}
