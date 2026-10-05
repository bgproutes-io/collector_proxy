#include "include/commands.h"
#include "include/debug.h"
#include "include/utils.h"
#include "include/common.h"
#include "include/config.h"
#include "include/timers.h"
#include "include/proxy_server.h"
#include "include/commands_def.h"

static void socket_peer_string(int socket_fd, char *dest, size_t dest_size)
{
    SS address = {0};
    socklen_t length = sizeof(address);
    if (socket_fd < 0 ||
        getpeername(socket_fd, (SA *)&address, &length) < 0 ||
        sockaddr_to_string(&address, dest, dest_size) < 0)
        snprintf(dest, dest_size, "<unknown-peer>");
}


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
        fprintf(stderr,
                "proxy startup error: invalid command line; expected --config <path>\n");
        return EXIT_FAILURE;
    }

    /* Parse the configuration file */
    if (Config_read(opt.configFile) == -1)
    {
        fprintf(stderr,
                "proxy startup error: configuration '%s' is invalid\n",
                opt.configFile);
        return EXIT_FAILURE;
    }

    /* Initialize the debug file */
    init_debug(config.log_file, config.debug_level);
    INFO(LOG_LEVEL_IMPORTANT,
         "Starting collector proxy: config=%s, log_level=%s (%d), pid=%ld",
         opt.configFile, log_level_name(global_debug.logLevel),
         global_debug.logLevel, (long)getpid());

    /* Initialize the values for the timers */
    timers = Timer_list_new();
    if (!timers)
    {
        ERROR(LOG_LEVEL_ALWAYS,
              "Unable to initialize background timer queue: %s (%d)",
              strerror(errno), errno);
        finish_debug();
        Config_cleanup();
        return EXIT_FAILURE;
    }
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

    /* Install the different commands */
    commands_init();
    INSTALL_CMD(exit_server, "exit-server", &exit_server);
    INSTALL_CMD(add_blacklisted_asn, "add asn <asn> blacklist", &add_blacklisted_asn);
    INSTALL_CMD(del_blacklisted_asn, "del asn <asn> blacklist", &del_blacklisted_asn);
    INSTALL_CMD(add_blacklisted_ip, "add ip <ip> blacklist", &add_blacklisted_ip);
    INSTALL_CMD(del_blacklisted_ip, "del ip <ip> blacklist", &del_blacklisted_ip);

    /* SELECT utils */
    fd_set socks;
    int nready;
    ssize_t size;
    int max_sock = 0;
    uint8_t buf[MAX_RECV_BUFF];
    memset(buf, 0, MAX_RECV_BUFF);
    socklen_t len;
    SS tmp_addr;

    while (cnt)
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
            ERROR(LOG_LEVEL_IMPORTANT,
                  "Event loop select failed: %s (%d); continuing",
                  strerror(errno), errno);
            continue;
        }

        /* handle case of connection attempt from router */
        if (!global_server->router_connected && FD_ISSET(global_server->listen_sock, &socks))
        {
            len = sizeof(SS);

            global_server->router_data_sock = accept(global_server->listen_sock, (SA*)&tmp_addr, &len);
            if (global_server->router_data_sock != -1)
            {
                char peer[INET6_ADDRSTRLEN + 16];
                socket_peer_string(global_server->router_data_sock, peer,
                                   sizeof(peer));
                INFO(LOG_LEVEL_IMPORTANT,
                     "Router connected: peer=%s, fd=%d, protocol=%s",
                     peer, global_server->router_data_sock,
                     global_server->proto == PROTOCOL_BMP ? "BMP" : "BGP");
                CircBuf_reset(&global_server->buffer);
                global_server->consecutive_parsing_errors = 0;
                global_server->router_connected = True;
            }
            else
            {
                WARNING(LOG_LEVEL_IMPORTANT,
                        "Unable to accept router connection: %s (%d)",
                        strerror(errno), errno);
            }
        }

        /* Handle case where a message is received */
        if (global_server->router_connected && FD_ISSET(global_server->router_data_sock, &socks))
        {
            memset(buf, 0, MAX_RECV_BUFF);
            size = recv(global_server->router_data_sock, buf, MAX_RECV_BUFF, 0);

            if (size > 0)
            {
                DEBUG(LOG_LEVEL_TOO_MUCH,
                      "Received router data: bytes=%zd, buffered_bmp_bytes=%u",
                      size, global_server->buffer.actLen);
                Proxy_server_process_router_message(global_server, buf, size);
            }
            else if (size == 0)
            {
                char peer[INET6_ADDRSTRLEN + 16];
                socket_peer_string(global_server->router_data_sock, peer,
                                   sizeof(peer));
                WARNING(LOG_LEVEL_IMPORTANT,
                        "Router disconnected cleanly: peer=%s, "
                        "discarding_buffered_bmp_bytes=%u",
                        peer, global_server->buffer.actLen);
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
                    char peer[INET6_ADDRSTRLEN + 16];
                    socket_peer_string(global_server->router_data_sock, peer,
                                       sizeof(peer));
                    ERROR(LOG_LEVEL_IMPORTANT,
                          "Router receive failed: peer=%s, error=%s (%d), "
                          "discarding_buffered_bmp_bytes=%u; closing connection",
                          peer, strerror(errno), errno,
                          global_server->buffer.actLen);
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
                char endpoint[INET6_ADDRSTRLEN + 16] = {0};
                sockaddr_to_string(&global_server->remote_addr, endpoint,
                                   sizeof(endpoint));
                WARNING(LOG_LEVEL_IMPORTANT,
                        "Collector disconnected cleanly: endpoint=%s; "
                        "automatic reconnect will be scheduled",
                        endpoint);
                Proxy_server_close_collector(global_server);
            }
            else if (size < 0)
            {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    char endpoint[INET6_ADDRSTRLEN + 16] = {0};
                    sockaddr_to_string(&global_server->remote_addr, endpoint,
                                       sizeof(endpoint));
                    ERROR(LOG_LEVEL_IMPORTANT,
                          "Collector receive failed: endpoint=%s, error=%s (%d); "
                          "closing connection and scheduling reconnect",
                          endpoint, strerror(errno), errno);
                    Proxy_server_close_collector(global_server);
                }
            }
            else
            {
                DEBUG(LOG_LEVEL_OPTIONAL,
                      "Received %zd unexpected bytes from collector; ignored",
                      size);
            }
        }

        /* Handle case where we receive a command connection request */
        if (!global_server->command_connected && FD_ISSET(global_server->listen_command_sock, &socks))
        {
            len = sizeof(SS);

            global_server->command_data_sock = accept(global_server->listen_command_sock, (SA*)&tmp_addr, &len);
            if (global_server->command_data_sock != -1)
            {
                char peer[INET6_ADDRSTRLEN + 16];
                socket_peer_string(global_server->command_data_sock, peer,
                                   sizeof(peer));
                INFO(LOG_LEVEL_IMPORTANT,
                     "Command client connected: peer=%s, fd=%d",
                     peer, global_server->command_data_sock);
                global_server->command_connected = True;
            }
            else
            {
                WARNING(LOG_LEVEL_IMPORTANT,
                        "Unable to accept command connection: %s (%d)",
                        strerror(errno), errno);
            }
        }

        /* Handle cases where we receive some data from the command socket */
        if (global_server->command_connected && FD_ISSET(global_server->command_data_sock, &socks))
        {
            memset(buf, 0, MAX_RECV_BUFF);
            size = recv(global_server->command_data_sock, buf, MAX_RECV_BUFF, 0);

            if (size > 0)
            {
                char command[MAX_COMMAND_SIZE] = {0};
                size_t command_length = (size_t)size;
                if (command_length >= sizeof(command))
                {
                    print_command_result(
                        global_server->command_data_sock,
                        COMMAND_FAILED,
                        "command exceeds maximum length");
                    continue;
                }
                memcpy(command, buf, command_length);
                while (command_length > 0 &&
                       (command[command_length - 1] == '\n' ||
                        command[command_length - 1] == '\r'))
                    command[--command_length] = '\0';

                int ret_cmd = execute_command(command);
                print_command_result(global_server->command_data_sock,
                                     ret_cmd, command);
                DEBUG(LOG_LEVEL_TOO_MUCH,
                      "Received command data: bytes=%zd", size);
            }
            else if (size == 0)
            {
                char peer[INET6_ADDRSTRLEN + 16];
                socket_peer_string(global_server->command_data_sock, peer,
                                   sizeof(peer));
                INFO(LOG_LEVEL_OPTIONAL,
                     "Command client disconnected: peer=%s", peer);
                close(global_server->command_data_sock);
                global_server->command_data_sock = -1;
                global_server->command_connected = False;
            }
            else
            {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    char peer[INET6_ADDRSTRLEN + 16];
                    socket_peer_string(global_server->command_data_sock, peer,
                                       sizeof(peer));
                    ERROR(LOG_LEVEL_IMPORTANT,
                          "Command receive failed: peer=%s, error=%s (%d); "
                          "closing connection",
                          peer, strerror(errno), errno);
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
