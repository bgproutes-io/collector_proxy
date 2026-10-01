#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <openssl/ssl.h>
#include <openssl/err.h>


#define DEFAULT "\x1B[0m"
#define RED     "\x1B[31m"
#define GREEN   "\x1B[32m"
#define YELLOW  "\x1B[33m"
#define BLUE    "\x1B[34m"
#define MAGENTA "\x1B[35m"
#define CYAN    "\x1B[36m"
#define WHITE   "\x1B[37m"

#define DUMP_MRT 1
#define DUMP_BZ2 2


#define MIN(a,b) ((a < b) ? a : b)
#define MAX(a,b) ((a > b) ? a : b)

#define bool uint8_t

#define False   0
#define True    1


#define _CONCAT2(a, b) a ## b
#define _CONCAT(a, b) _CONCAT2(a,b)
#define NAMECTR(name) _CONCAT(name, __COUNTER__)



# define RAND(min, max) \
    ((rand()%(int)(((max))-(min)))+ (min))

#define UNUSED(x) (void)(x)


/* Complies to RFC 8654 */
#define MAX_BGP_MESSAGE_SIZE    65536   

/* Max number of chars in a directory path */
#define MAX_CHAR_DIRECTORY  4096

/* Max number of chars in an IP address */
#define MAX_IP_LENGTH   64
#define MAX_PFX_LENGTH  MAX_IP_LENGTH + 4
#define MAX_PEER_LENGTH 128
#define MAX_NLRI 4096
#define MAX_RECV_BUFF  8192 * 2

#define MAX_STATE_MESSAGE 2048
#define MAX_RIB_ENTRY_LENGTH 65535

#define MAX_STATE_MESSAGE_ID    1000000
#define FORCE_END_OF_RIB_DELAY  1800
#define MAX_CONSECUTIVE_PARSING_ERRORS 10

#define MAX_AFI_VALID  64
#define MAX_SAFI_VALID 64




#define measure_exec_time(cmd)   \
    do {                                \
        struct timeval start, stop;     \
        gettimeofday(&start, NULL);     \
                                        \
        cmd;                            \
                                        \
        gettimeofday(&stop, NULL);      \
        printf("Command '%s' took %f seconds\n", #cmd, (double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_usec - start.tv_usec) / 1000000.0);  \
    } while(0)
    


#define _RESULTS(...)\
    fprintf(stderr,"[" BLUE "RESULTS" DEFAULT "] : " __VA_ARGS__); \
    fprintf(stderr, DEFAULT  "\n"); \

#define _INFO(...)\
    fprintf(stderr,"[" GREEN "INFO" DEFAULT "] : " __VA_ARGS__ ); \
    fprintf(stderr, DEFAULT  "\n"); \

#define _WARNING(...)\
    fprintf(stderr,YELLOW "WARNING : " DEFAULT __VA_ARGS__); \
    fprintf(stderr, DEFAULT  "\n"); \

#define _DEBUG(...) \
    fprintf(stderr,BLUE "DEBUG - file : \"%s\", line : %d, function \"%s\". " DEFAULT  __VA_ARGS__ "\n",__FILE__,__LINE__,__func__); 

#define _ERROR(...)\
    fprintf(stderr, "[" RED "ERROR" DEFAULT "] : " MAGENTA __VA_ARGS__ ); \
    fprintf(stderr, DEFAULT  "\n"); \


#define ASSERT(value, code,...)\
    if(!value) {\
        fprintf(stderr,RED "ASSERTION FAILED - file : \"%s\", line : %d, function \"%s\". " MAGENTA  __VA_ARGS__ "\n",__FILE__,__LINE__,__func__); \
        return code;\
    }


#define MAX_PFXS_IN_NLRI    2048
#define ATTR_BUFF_LEN (INET6_ADDRSTRLEN + 4) * MAX_PFXS_IN_NLRI * 2
#define MAX_STRING_BUFF_LEN ATTR_BUFF_LEN * 2


typedef enum {
    PROTOCOL_BGP = 0,
    PROTOCOL_BMP
} Peering_protocol_t;


#endif
