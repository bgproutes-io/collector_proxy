#ifndef __COMMANDS_DEF_H__
#define __COMMANDS_DEF_H__

#include "common.h"

extern int cnt;
extern struct sockaddr cmdClient;
extern int commandSock;
extern int commandSockConn;


int exit_server(int argc, char **argv);

int add_blacklisted_asn(int argc, char** argv);
int del_blacklisted_asn(int argc, char** argv);

int add_blacklisted_ip(int argc, char** argv);
int del_blacklisted_ip(int argc, char** argv);

#endif