#include "include/commands.h"
#include "include/debug.h"
#include "include/utils.h"
#include "include/common.h"
#include "include/config.h"
#include "include/timers.h"
#include "include/proxy_server.h"



int main_prepare_select_sockets(int* max_sock, fd_set* socks)
{
    /* Setup the file descriptor list */
    (*max_sock) = STDIN_FILENO;
    FD_ZERO(socks);
    FD_SET(STDIN_FILENO, socks);

    if (!global_server->router_connected)
    {
        FD_SET(global_server->listen_sock, socks);
        (*max_sock) = MAX(global_server->listen_sock, (*max_sock));
    }
    else
    {
        FD_SET(global_server->router_data_sock, socks);
        (*max_sock) = MAX(global_server->router_data_sock, (*max_sock));
    }
    
    if (global_server->collector_connected)
    {
        FD_SET(global_server->collector_data_sock, socks);
        (*max_sock) = MAX(global_server->collector_data_sock, (*max_sock));
    }

    if (!global_server->command_connected)
    {
        FD_SET(global_server->listen_command_sock, socks);
        (*max_sock) = MAX(global_server->listen_command_sock, (*max_sock));
    }
    else
    {
        FD_SET(global_server->command_data_sock, socks);
        (*max_sock) = MAX(global_server->command_data_sock, (*max_sock));
    }

    return 0;
}




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
    init_debug(config.log_file, config.debug_level);

    /* Initialize the values for the timers */
    timers = Timer_list_new();
    struct timeval tv;
    tv.tv_usec = 0;

    /* Try to instanciate the main proxy server */
    global_server = Proxy_server_new(&config);
    if (!global_server)
    {
        ERROR(LOG_LEVEL_ALWAYS, "Unable to create the global Proxy server instance.");
        Timer_list_free(timers);
        finish_debug();
        Config_cleanup();
        return EXIT_FAILURE;
    }

    int stop = False;

    /* SELECT utils */
    fd_set socks;
    int nready;
    ssize_t size;
    int max_sock = 0;
    uint8_t buf[MAX_RECV_BUFF];
    memset(buf, 0, MAX_RECV_BUFF);
    socklen_t len;
    SS tmp_addr;

    while (!stop)
    {
        /* Start the loop by processing all pending background tasks */
        tv.tv_sec = Timer_list_process(timers);

        main_prepare_select_sockets(&max_sock, &socks);

        /* If there is no background task remaining */
        if (tv.tv_sec == INT64_MAX)
        {
            nready = select(max_sock+1, &socks, NULL, NULL, NULL);
        }
        /* Otherwise activate the timeout in the select */
        else
        {
            nready = select(max_sock+1, &socks, NULL, NULL, &tv);
        }

        if (nready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ERROR(LOG_LEVEL_ALWAYS, "Error on select: '%s'.", strerror(errno));
            continue;
        }

        /* handle case of connection attempt from router */
        if (!global_server->router_connected && FD_ISSET(global_server->listen_sock, &socks))
        {
            len = sizeof(SS);

            global_server->router_data_sock = accept(global_server->listen_sock, (SA*)&tmp_addr, &len);
            if (global_server->router_data_sock != -1)
            {
                DEBUG(LOG_LEVEL_IMPORTANT, "Router correctly connected to the proxy.");
                CircBuf_reset(&global_server->buffer);
                global_server->consecutive_parsing_errors = 0;
                global_server->router_connected = True;
            }
        }

        /* Handle case where a message is received */
        if (global_server->router_connected && FD_ISSET(global_server->router_data_sock, &socks))
        {
            memset(buf, 0, MAX_RECV_BUFF);
            size = recv(global_server->router_data_sock, buf, MAX_RECV_BUFF, 0);

            if (size > 0)
            {
                Proxy_server_process_router_message(global_server, buf, size);
            }
            else if (size == 0)
            {
                WARNING(LOG_LEVEL_IMPORTANT, "Connection with the router is down.");
                close(global_server->router_data_sock);
                CircBuf_reset(&global_server->buffer);
                global_server->consecutive_parsing_errors = 0;
                global_server->router_data_sock = -1;
                global_server->router_connected = False;
            }
            else
            {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    ERROR(LOG_LEVEL_ALWAYS, "We received an error when receiving BMP messages: '%s'.", strerror(errno));
                    close(global_server->router_data_sock);
                    CircBuf_reset(&global_server->buffer);
                    global_server->consecutive_parsing_errors = 0;
                    global_server->router_data_sock = -1;
                    global_server->router_connected = False;
                }
            }
        }

        /* Handle case we receive data from the collector (likely disconnection) */
        if (global_server->collector_connected && FD_ISSET(global_server->collector_data_sock, &socks))
        {
            memset(buf, 0, MAX_RECV_BUFF);
            size = Proxy_server_read(global_server, buf, MAX_RECV_BUFF);

            if (size == 0)
            {
                WARNING(LOG_LEVEL_IMPORTANT, "Connection with the collector is down.");
                Proxy_server_close_collector(global_server);
            }
            else if (size < 0)
            {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    ERROR(LOG_LEVEL_ALWAYS, "We received an error when receiving BMP messages: '%s'.", strerror(errno));
                    Proxy_server_close_collector(global_server);
                }
            }
        }

        /* Handle case where we receive a command connection request */
        if (!global_server->command_connected && FD_ISSET(global_server->listen_command_sock, &socks))
        {
            len = sizeof(SS);

            global_server->command_data_sock = accept(global_server->listen_command_sock, (SA*)&tmp_addr, &len);
            if (global_server->command_data_sock != -1)
            {
                DEBUG(LOG_LEVEL_IMPORTANT, "Command helper correctly connected to the proxy.");
                global_server->command_connected = True;
            }
        }

        /* Handle cases where we receive some data from the command socket */
        if (global_server->command_connected && FD_ISSET(global_server->command_data_sock, &socks))
        {
            memset(buf, 0, MAX_RECV_BUFF);
            size = recv(global_server->command_data_sock, buf, MAX_RECV_BUFF, 0);

            if (size > 0)
            {

            }
            else if (size == 0)
            {
                WARNING(LOG_LEVEL_OPTIONAL, "Connection with the command shell is down.");
                close(global_server->command_data_sock);
                global_server->command_data_sock = -1;
                global_server->command_connected = False;
            }
            else
            {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    ERROR(LOG_LEVEL_ALWAYS, "We received an error when receiving command: '%s'.", strerror(errno));
                    close(global_server->command_data_sock);
                    global_server->command_data_sock = -1;
                    global_server->command_connected = False;
                }
            }
        }
    }

    Timer_list_free(timers);
    Proxy_server_free(global_server);
    command_finish();
    finish_debug();
    Config_cleanup();

    return 0;
}
