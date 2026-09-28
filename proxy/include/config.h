#ifndef __CONFIG_H__
#define __CONFIG_H__

/**
 * @file config.h
 * @brief Configuration loader for the BGP/BMP collector.
 *
 * This module provides:
 *  - CLI option parsing (`option_command_parser`)
 *  - The main configuration structure `Config_t`
 *  - Support structures for declaring remote peers (`Remote_peer_t`)
 *  - Default values for various runtime parameters
 *
 * The configuration file is parsed by `Config_read()` and supports
 * two operational modes:
 *      [bgp]
 *      [bmp]
 *
 * Depending on the selected mode, the parser accepts different
 * configuration blocks and allows definition of BGP peers or
 * BMP collector parameters.
 *
 * All results are stored in the global `config` object.
 */

#include "common.h"
#include "utils.h"

/* ========================================================================== */
/*                             CLI OPTION HANDLING                             */
/* ========================================================================== */

/**
 * @struct options
 * @brief Runtime options parsed from command line arguments.
 *
 * Currently only supports:
 *   - --config <file>
 */
struct options 
{
    char *configFile;    /**< Path to configuration file */
};

extern struct options opt;

/**
 * @brief Parse CLI arguments and populate the global `opt` structure.
 *
 * Supported arguments:
 *   - -c, --config <file> : Use alternate configuration file
 *
 * If no configuration file is specified, DEFAULT_CONFIG_FILE is used.
 *
 * @return 0 on success, -1 on error.
 */
extern int option_command_parser(int argc, char** argv);

/* ========================================================================== */
/*                                DEFAULT VALUES                               */
/* ========================================================================== */

#define DEFAULT_CONFIG_FILE         "proxy.cfg"
#define DEFAULT_CLIENT_CRT          "client.crt"
#define DEFAULT_CLIENT_KEY          "client.key"
#define DEFAULT_CA_CRT              "ca.crt"


/* ========================================================================== */
/*                               MAIN CONFIG STRUCT                            */
/* ========================================================================== */

/**
 * @struct config_s
 * @brief Full collector configuration loaded from file.
 *
 * The structure contains general options shared between BGP/BMP modes
 * as well as specialized sections depending on protocol mode.
 */
typedef struct config_s 
{
    int debug_level;
    char log_file[MAX_CHAR_DIRECTORY];

    SS   local_addr;
    char local_addr_string[MAX_IP_LENGTH];
    int  local_port;
    SS   remote_addr;
    char remote_addr_string[MAX_IP_LENGTH];
    int  remote_port;

    /* --------------------  SSL INFO --------------------- */
    bool use_tls;
    char client_crt[MAX_CHAR_DIRECTORY];
    char client_key[MAX_CHAR_DIRECTORY];
    char ca_crt[MAX_CHAR_DIRECTORY];
}
Config_t;

/**
 * @brief Global runtime configuration loaded by Config_read().
 */
extern Config_t config;

/**
 * @brief Parse the configuration file and populate global `config`.
 *
 * The parser:
 *  - Loads defaults
 *  - Reads [bgp] or [bmp] mode
 *  - Parses key:value pairs
 *  - In BGP mode, extracts peer blocks (“-peer … -end”)
 *  - Performs mandatory checks on essential fields
 *
 * @param file Path to configuration file
 * @return 0 on success, -1 on error.
 */
int Config_read(const char* file);

#endif
