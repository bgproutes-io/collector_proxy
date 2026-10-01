#include "include/commands.h"
#include "include/debug.h"
#include "include/utils.h"
#include "include/common.h"
#include "include/config.h"


int main(int argc, char** argv)
{
    if (option_command_parser(argc, argv) == -1)
    {
        printf("Unable to parse the option command line. Please check the help.\n");
        return EXIT_FAILURE;
    }

    /* Parse the configuration file */
    if (Config_read(opt.configFile) == -1)
    {
        printf("Error when parsing the configuration file, go fix it!\n");
        return EXIT_FAILURE;
    }

    /* Initialize the debug file */
    init_debug(&config, LOG_LEVEL_IMPORTANT);

    

    return 0;
}
