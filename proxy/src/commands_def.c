#include "../include/commands_def.h"
#include "../include/utils.h"
#include "../include/commands.h"
#include "../include/proxy_server.h"



int cnt = True;

static void refresh_bmp_filter_state(void)
{
    global_server->use_bmp_filters =
        global_server->proto == PROTOCOL_BMP &&
        (global_server->cfg->blacklisted_asns->count +
         global_server->cfg->blacklisted_ips->count > 0);
}

static int parse_asn(const char *text, uint32_t *asn)
{
    if (!text || !*text || text[0] == '-')
        return -1;

    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || value > UINT32_MAX)
        return -1;

    *asn = (uint32_t)value;
    return 0;
}

int exit_server(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cnt = False;
    return COMMAND_OK;
}



int add_blacklisted_asn(int argc, char** argv)
{
    if (argc != 1)
    {
        return COMMAND_INCOMPLETE;
    }

    uint32_t asn;
    if (parse_asn(argv[0], &asn) < 0 ||
        Blacklist_add_asn(global_server->cfg->blacklisted_asns, asn) < 0)
    {
        return COMMAND_FAILED;
    }

    refresh_bmp_filter_state();

    return COMMAND_OK;
}



int del_blacklisted_asn(int argc, char** argv)
{
    if (argc != 1)
    {
        return COMMAND_INCOMPLETE;
    }

    uint32_t asn;
    if (parse_asn(argv[0], &asn) < 0 ||
        Blacklist_del_asn(global_server->cfg->blacklisted_asns, asn) < 0)
    {
        return COMMAND_FAILED;
    }

    refresh_bmp_filter_state();

    return COMMAND_OK;
}




int add_blacklisted_ip(int argc, char** argv)
{
    if (argc != 1)
    {
        return COMMAND_INCOMPLETE;
    }

    char* ip = argv[0];

    if (Blacklist_add_ip(global_server->cfg->blacklisted_ips, ip) == -1)
    {
        return COMMAND_FAILED;
    }

    refresh_bmp_filter_state();

    return COMMAND_OK;
}



int del_blacklisted_ip(int argc, char** argv)
{
    if (argc != 1)
    {
        return COMMAND_INCOMPLETE;
    }

    char* ip = argv[0];

    if (Blacklist_del_ip(global_server->cfg->blacklisted_ips, ip) == -1)
    {
        return COMMAND_FAILED;
    }

    refresh_bmp_filter_state();

    return COMMAND_OK;
}
