#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <strings.h>

#include "../include/config.h"
#include "../include/common.h"

Config_t config;
struct options opt;


int option_command_parser(int argc, char** argv)
{
    memset(&opt, 0, sizeof(struct options));

    int optValue = 0;
    const char* optstring = "c";
    const struct option long_options[] = {
        {"config",              required_argument,  NULL,   'c'},
        {"graceful_restart",    no_argument,        NULL,   'g'},
        {"vp",                  required_argument,  NULL,   'v'},
        {0,0,0,0}
    };

    while ((optValue = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
        switch(optValue)
        {
            case 'c':
                opt.configFile = optarg;
                break;

            case 'v':
                break;

            default:
                return -1;
                break;
        }
    }

    if (!opt.configFile)
    {
        opt.configFile = DEFAULT_CONFIG_FILE;
    }

    return 0;
}

/* ----------------------- STRING HELPERS ----------------------- */

static char* trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}



static int split_param(char *line, char **param, char **value)
{
    char *p = strchr(line, ':');
    if (!p) return 0;
    *p = 0;
    *param = trim(line);
    *value = trim(p + 1);
    return 1;
}

/* ----------------------- DEFAULT VALUES ----------------------- */

static void Config_set_defaults(Config_t *cfg)
{
    memset(cfg, 0, sizeof(Config_t));

    strcpy(cfg->client_crt, DEFAULT_CLIENT_CRT);
    strcpy(cfg->client_key, DEFAULT_CLIENT_KEY);
    strcpy(cfg->ca_crt, DEFAULT_CA_CRT);
}


/* ----------------------- LIST PARSER ----------------------- */

static int parse_comma_asn_list(const char *str, Blacklist_t *out)
{
    char *copy = strdup(str);
    if (!copy)
        return -1;

    char *saveptr = NULL;
    for (char *token = strtok_r(copy, ",", &saveptr); token;
         token = strtok_r(NULL, ",", &saveptr)) {
        token = trim(token);
        char *end = NULL;
        errno = 0;
        unsigned long long asn = strtoull(token, &end, 10);
        if (!*token || errno || *trim(end) || asn > UINT32_MAX ||
            Blacklist_add_asn(out, (uint32_t)asn) < 0) {
            free(copy);
            return -1;
        }
    }
    free(copy);
    return 0;
}

static int parse_comma_ip_list(const char *str, Blacklist_t *out)
{
    char *copy = strdup(str);
    if (!copy)
        return -1;

    char *saveptr = NULL;
    for (char *token = strtok_r(copy, ",", &saveptr); token;
         token = strtok_r(NULL, ",", &saveptr)) {
        token = trim(token);
        if (!*token || Blacklist_add_ip(out, token) < 0) {
            free(copy);
            return -1;
        }
    }
    free(copy);
    return 0;
}

void Config_cleanup(void)
{
    Blacklist_free(config.blacklisted_asns);
    Blacklist_free(config.blacklisted_ips);
    config.blacklisted_asns = NULL;
    config.blacklisted_ips = NULL;
}

bool Config_is_asn_blacklisted(uint32_t asn)
{
    return Blacklist_contains_asn(config.blacklisted_asns, asn);
}

bool Config_is_ip_blacklisted(const SS *ip)
{
    return Blacklist_contains_ip(config.blacklisted_ips, ip);
}

/* ----------------------- MAIN CONFIG PARSER ----------------------- */

int Config_read(const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f) {
        fprintf(stderr, "Could not open config file: %s\n", file);
        return -1;
    }

    Config_cleanup();
    Config_set_defaults(&config);
    config.blacklisted_asns = Blacklist_new();
    config.blacklisted_ips = Blacklist_new();
    if (!config.blacklisted_asns || !config.blacklisted_ips) {
        _ERROR("Unable to allocate configuration blacklists\n");
        Config_cleanup();
        fclose(f);
        return -1;
    }

    char line[2048];
    while (fgets(line, sizeof(line), f)) {

        char *s = trim(line);
        if (*s == 0 || *s == '#')
            continue;

        /* ---- PARAMETER LINE ---- */
        char *param, *value;
        if (!split_param(s, &param, &value)) {
            fprintf(stderr, "Invalid line: %s\n", s);
            fclose(f);
            Config_cleanup();
            return -1;
        }

        /* ---- COMMON PARAMETERS ---- */
        if (strcmp(param, "debug_level") == 0)
            config.debug_level = atoi(value);
        else if (strcmp(param, "log_file") == 0)
            strncpy(config.log_file, value, MAX_CHAR_DIRECTORY-1);
        else if (strcmp(param, "local_addr") == 0) {
            ip_to_sockaddr(value, &config.local_addr, 0);
            strncpy(config.local_addr_string, value, MAX_IP_LENGTH-1);
        }
        else if (strcmp(param, "local_port") == 0) {
            config.local_port = atoi(value);
        }
        else if (strcmp(param, "collector_ip") == 0) {
            ip_to_sockaddr(value, &config.remote_addr, 0);
            strncpy(config.remote_addr_string, value, MAX_IP_LENGTH-1);
        }
        else if (strcmp(param, "collector_port") == 0) {
            config.remote_port = atoi(value);
        }
        else if (strcmp(param, "command_host") == 0) {
            ip_to_sockaddr(value, &config.command_addr, 0);
            strncpy(config.command_addr_string, value, MAX_IP_LENGTH-1);
        }
        else if (strcmp(param, "command_port") == 0) {
            config.command_port = atoi(value);
        }
        else if (strcmp(param, "use_tls") == 0)
            config.use_tls = atoi(value);
        else if (strcmp(param, "blacklisted_asns") == 0) {
            if (parse_comma_asn_list(value, config.blacklisted_asns) < 0) {
                fprintf(stderr, "Invalid blacklisted_asns value: %s\n", value);
                fclose(f);
                Config_cleanup();
                return -1;
            }
        }
        else if (strcmp(param, "blaclisted_ips") == 0 ||
                 strcmp(param, "blacklisted_ips") == 0) {
            if (parse_comma_ip_list(value, config.blacklisted_ips) < 0) {
                fprintf(stderr, "Invalid %s value: %s\n", param, value);
                fclose(f);
                Config_cleanup();
                return -1;
            }
        }
        else if (strcmp(param, "proto") == 0) {
            if (strcasecmp(value, "bgp") == 0)
                config.proto = PROTOCOL_BGP;
            else if (strcasecmp(value, "bmp") == 0)
                config.proto = PROTOCOL_BMP;
            else {
                fprintf(stderr, "Invalid proto value: %s (expected bgp or bmp)\n", value);
                fclose(f);
                Config_cleanup();
                return -1;
            }
            config.proto_configured = True;
        }
    }

    fclose(f);

    /* ---------------- MANDATORY CHECKS ---------------- */

    if (config.local_addr_string[0] == 0 ||
        config.local_port == 0 ||
        config.remote_addr_string[0] == 0 ||
        config.remote_port == 0)
    {
        _ERROR("Missing mandatory general parameters\n");
        Config_cleanup();
        return -1;
    }

    return 0;
}
