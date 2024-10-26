#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include <stddef.h>
#include <sys/types.h>

// 1) Send uint16_t, int32_t etc., not int.
//    The length of int is platform-dependent.
// 2) If we want to send a structure, we have to declare it
//    with __attribute__((__packed__)). Otherwise the compiler
//    may add a padding bewteen fields. In the following example
//    sizeof (data_pkt) is then 8, not 6.

#define MAX_PACKAGE_SIZE 64000
#define CONN 1
#define CONACC 2
#define CONRJT 3
#define DATA 4
#define ACC 5
#define RJT 6
#define RCVD 7
#define TCP 1
#define UDP 2
#define UDPR 3
#define QUEUE_LENGTH  5
#define SOCK_TIMEOUT  4

typedef struct __attribute__((__packed__)) {
    uint16_t seq_no;
    uint32_t number;
} data_pkt;

typedef struct __attribute__((__packed__)) {
    uint64_t sum;
} response_pkt;

typedef struct __attribute__((__packed__)) conn{
    uint8_t id_pakietu;
    uint64_t id_sesji;
    uint8_t id_protokolu;
    uint64_t dlugosc;
} conn;

typedef struct __attribute__((__packed__)) conacc{
    uint8_t id_pakietu;
    uint64_t id_sesji;
} conacc;

typedef struct __attribute__((__packed__)) conrjt{
    uint8_t id_pakietu;
    uint64_t id_sesji;
} conrjt;

typedef struct __attribute__((__packed__)) data{
    uint8_t id_pakietu;
    uint64_t id_sesji;
    uint64_t nr_pakietu;
    uint32_t liczba_bajtow;
} data;

typedef struct __attribute__((__packed__)) acc{
    uint8_t id_pakietu;
    uint64_t id_sesji;
    uint64_t nr_pakietu;
} acc;

typedef struct __attribute__((__packed__)) rjt{
    uint8_t id_pakietu;
    uint64_t id_sesji;
    uint64_t nr_pakietu;
} rjt;

typedef struct __attribute__((__packed__)) rcvd{
    uint8_t id_pakietu;
    uint64_t id_sesji;
} rcdv;

ssize_t	readn(int fd, void *vptr, size_t n);
ssize_t	writen(int fd, const void *vptr, size_t n);
struct sockaddr_in get_server_address(char const *host, uint16_t port);

#endif // SUM_COMMON_H
