#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>

#ifndef __cplusplus
#include "bool.h"
#endif

#ifndef CT_MSG_H
#include "message.h"
#endif

#ifdef _WIN32
//#include <windows.h>
#include <WinSock2.h>
#pragma comment(lib,"Ws2_32.lib") 
typedef SOCKET socket_t;
typedef int socklen_t;

#else
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

typedef int socket_t;
#define closesocket close
#endif

#ifdef _WIN32
#define SOCK_ADDR_IP(sa)    (sa).sin_addr.S_un.S_addr
#else
#define SOCK_ADDR_IP(sa)    (sa).sin_addr.s_addr
#endif



/* Free and set set the variable to NULL */
#define FREENULL(a) do { free( a ); a = 0; } while(0)

#define PRIu32    "%lu"
#define PRIx32    "%lx"

/* Input */
typedef struct access_t access_t;
typedef struct access_sys_t access_sys_t;

/* Block */
typedef struct block_t block_t;
typedef struct decode_t decode_t;

/******************************************************************
 * Error values
 *****************************************************************/
#define VLC_SUCCESS -0
#define VLC_ENOMEM -1
#define VLC_ETIMEOUT -3
#define VLC_EGENERIC -666

/* ************************************************************** */
#ifndef __MAX
#   define __MAX(a, b)   ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef __MIN
#   define __MIN(a, b)   ( ((a) < (b)) ? (a) : (b) )
#endif

static inline uint16_t GetWLE(const void *_p)
{
       const uint8_t *p = (const uint8_t *)_p;
       return (((uint16_t)p[1] << 8) | p[0]);
}

static inline uint32_t GetDWLE(const void *_p)
{
       const uint8_t *p = (const uint8_t *)_p;
       return (((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16)
              | ((uint32_t)p[1] << 8) | p[0]);
}
#endif
