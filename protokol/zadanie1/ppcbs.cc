#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <malloc.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <ctime>


#include "common.h"
#include "err.h"
#include "protconst.h"
int timeouts;

struct sockaddr_in get_server_address(char const *host, uint16_t port) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;  // IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo *address_result;
  int errcode = getaddrinfo(host, NULL, &hints, &address_result);
  if (errcode != 0) {
    fatal("getaddrinfo: %s", gai_strerror(errcode));
  }

  struct sockaddr_in send_address;
  send_address.sin_family = AF_INET;  // IPv4
  send_address.sin_addr.s_addr =      // IP address
      ((struct sockaddr_in *)(address_result->ai_addr))->sin_addr.s_addr;
  send_address.sin_port = htons(port);  // port from the command line

  freeaddrinfo(address_result);

  return send_address;
}

void send_rjt_tcp(int socket_fd, struct sockaddr_in client_address,
                  int id_sesji, uint64_t nr_paczki) {
  struct rjt resp;
  resp.id_sesji = id_sesji;
  resp.id_pakietu = RJT;
  resp.nr_pakietu = nr_paczki;
  int written_length = writen(socket_fd, &resp, sizeof resp);
  if ((size_t)written_length < sizeof resp) {
    std::cerr << "ERROR: Problem while writing rejecting response" << std::endl;
  } else {
    // printf("Sent RJT\n");
  }
}

void send_conrjt_tcp(int socket_fd, struct sockaddr_in client_address,
                     int id_sesji) {
  struct conrjt resp;
  resp.id_sesji = id_sesji;
  resp.id_pakietu = CONRJT;
  int written_length = writen(socket_fd, &resp, sizeof resp);
  if ((size_t)written_length < sizeof resp) {
    std::cerr << "ERROR: Problem while writing rejecting response" << std::endl;
  } else {
    // printf("Sent CONRJT\n");
  }
}

bool recieve_conn_tcp(int socket_fd, struct sockaddr_in client_address,
                      struct conn *info_ptr) {
  struct conn info;
  int read_length = readn(socket_fd, &info, sizeof info);
  if (read_length < 0) {
    if (errno == EAGAIN) {
      // std::cerr << "Timeout" << std::endl;
      timeouts ++;
      return false;
    } else {
      std::cerr << "ERROR: Problem while reading" << std::endl;
      return false;
    }
  } else if (read_length == 0) {
    std::cerr << "ERROR: Connection closed" << std::endl;
    return false;
  } else if ((size_t)read_length < sizeof info) {
    std::cerr
        << "ERROR: Connection closed without providing full data structure"
        << std::endl;
    return false;
  } else {
    // printf("recieved CONN\n");
  }
  if (info.id_pakietu != CONN) {
    std::cerr << "ERROR: Wrong package id" << std::endl;
    return false;
  }
  if (info.id_protokolu != TCP) {
    std::cerr << "ERROR: Wrong protocol id" << std::endl;
    return false;
  }
  if (info.dlugosc == 0) {
    std::cerr << "ERROR: Empty message" << std::endl;
    return false;
  }
  (*info_ptr) = info;
  return true;
}

bool send_conacc_tcp(int socket_fd, struct conn info) {
  struct conacc resp;
  resp.id_pakietu = CONACC;
  resp.id_sesji = info.id_sesji;
  int written_length = writen(socket_fd, &resp, sizeof resp);
  if ((size_t)written_length < sizeof resp) {
    std::cerr << "ERROR: Problem while writing accepting response" << std::endl;
    return false;
  } else {
    // printf("sent CONACC\n");
  }

  return true;
}

bool send_rcvd_tcp(int socket_fd, struct conn info) {
  struct rcvd resp_rcvd;
  resp_rcvd.id_pakietu = RCVD;
  resp_rcvd.id_sesji = info.id_sesji;
  int written_length = writen(socket_fd, &resp_rcvd, sizeof resp_rcvd);
  if ((size_t)written_length < sizeof resp_rcvd) {
    std::cerr
        << "ERROR: Problem while writing full message response information"
        << std::endl;
    return false;
  } else {
    // printf("Sent RCVD\n");
  }
  return true;
}

void handle_tcp(int socket_fd, struct sockaddr_in client_address) {
  char const *client_ip = inet_ntoa(client_address.sin_addr);
  uint16_t client_port = ntohs(client_address.sin_port);

  struct timeval tv;
  tv.tv_sec = MAX_WAIT;
  tv.tv_usec = 0;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("Error");
  }

  printf("accepted connection from %s:%" PRIu16 "\n", client_ip, client_port);

  int read_length, written_length;

  // Reading CONN
  struct conn info;
  if (!recieve_conn_tcp(socket_fd, client_address, &info)) {
    return;
  }

  // Sending CONACC
  if (!send_conacc_tcp(socket_fd, info)) {
    return;
  }

  int bytes_to_read = info.dlugosc;
  int nr_paczki = 0;
  char *dane;
  int offset = 0;
  dane = (char *)malloc(sizeof(char) * info.dlugosc);
  // Reading DATA
  while (bytes_to_read > 0) {
    struct data data;
    read_length = readn(socket_fd, &data, sizeof data);
    if (read_length < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // std::cerr << "Timeout" << std::endl;
        timeouts ++;
        return;
      } else {
        std::cerr << "ERROR: Problem while reading" << std::endl;
        send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
        return;
      }
    } else if (read_length == 0) {
      std::cerr << "ERROR: Connection closed" << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    } else if ((size_t)read_length < sizeof info) {
      std::cerr
          << "ERROR: Connection closed without providing full data structure"
          << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    } else {
      // printf("recieved HEADER of package nr: %d\n", nr_paczki);
    }

    if (data.id_pakietu != DATA) {
      std::cerr << "ERROR: Wrong package id" << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    }
    if (data.nr_pakietu != nr_paczki) {
      std::cerr << "ERROR: Wrong package number" << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    }
    if (data.id_sesji != info.id_sesji) {
      std::cerr << "ERROR: Wrong session" << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    }
    if (data.liczba_bajtow == 0) {
      std::cerr << "ERROR: Empty message" << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    }
    if (data.liczba_bajtow > MAX_PACKAGE_SIZE) {
      std::cerr << "ERROR: Too big message" << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    }

    bytes_to_read -= data.liczba_bajtow;
    read_length = readn(socket_fd, dane + offset, data.liczba_bajtow);
    if (read_length < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // std::cerr << "Timeout" << std::endl;
        timeouts ++;
        return;
      } else {
        std::cerr << "ERROR: Problem while reading" << std::endl;
        send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
        return;
      }
    } else if (read_length == 0) {
      std::cerr << "ERROR: Connection closed" << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    } else if ((size_t)read_length < data.liczba_bajtow) {
      std::cerr
          << "ERROR: Connection closed without providing full data structure"
          << std::endl;
      send_rjt_tcp(socket_fd, client_address, info.id_sesji, nr_paczki);
      return;
    } else {
      // printf("recieved DATA from package nr: %d\n", nr_paczki);
    }
    offset += data.liczba_bajtow;
    nr_paczki++;
  }

  // Sending RCVD
  if (!send_rcvd_tcp(socket_fd, info)) {
    return;
  }

    printf("%s", dane);
}

void send_conrjt_udp(int socket_fd, struct sockaddr_in client_addr,
                     socklen_t client_addr_len, uint64_t id_sesji) {
  struct conrjt resp;
  resp.id_pakietu = CONRJT;
  resp.id_sesji = id_sesji;
  int sent_length = sendto(socket_fd, &resp, sizeof resp, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
  if (sent_length < 0) {
    std::cerr << "ERROR: Problem with sending CONRJT" << std::endl;
    syserr("sendto");
  } else if (sent_length != (ssize_t)sizeof resp) {
    std::cerr << "ERROR: Incomplete sending of CONRJT" << std::endl;
    fatal("incomplete sending");
  } else {
    // printf("Sent CONRJT\n");
  }
}

void send_rjt_udp(int socket_fd, struct sockaddr_in client_addr,
                  socklen_t client_addr_len, uint64_t id_sesji,
                  uint64_t nr_paczki) {
  struct rjt resp;
  resp.id_pakietu = RJT;
  resp.id_sesji = id_sesji;
  resp.nr_pakietu = nr_paczki;
  int sent_length = sendto(socket_fd, &resp, sizeof resp, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
  if (sent_length < 0) {
    std::cerr << "ERROR: Problem with sending RJT" << std::endl;
    syserr("sendto");
  } else if (sent_length != (ssize_t)sizeof resp) {
    std::cerr << "ERROR: Incomplete sending of RJT" << std::endl;
    fatal("incomplete sending");
  } else {
    // printf("Sent RJT\n");
  }
}

bool recieve_conn_udp(int socket_fd, struct conn *info_ptr,
                      struct sockaddr_in client_addr,
                      socklen_t client_addr_len) {
  struct conn info;
  int recieved_length =
      recvfrom(socket_fd, &info, sizeof info, 0,
               (struct sockaddr *)&client_addr, &client_addr_len);
  if (recieved_length < 0) {
    std::cerr << "ERROR: Problem with reading CONN " << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return false;
  } else {
    // printf("Recieved CONN\n");
  }

  if (info.id_pakietu != CONN) {
    std::cerr << "ERROR: Wrong package id" << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return false;
  }
  if (info.id_protokolu != UDP) {
    std::cerr << "ERROR: Wrong protocol id" << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return false;
  }
  if (info.dlugosc == 0) {
    std::cerr << "ERROR: Empty message" << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return false;
  }
  (*info_ptr) = info;
  return true;
}

bool send_conacc_udp(int socket_fd, struct conn info,
                     struct sockaddr_in client_addr,
                     socklen_t client_addr_len) {
  struct conacc resp;
  resp.id_pakietu = CONACC;
  resp.id_sesji = info.id_sesji;

  int sent_length = sendto(socket_fd, &resp, sizeof resp, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
  if (sent_length < 0) {
    std::cerr << "ERROR: Problem with sending CONACC" << std::endl;
    return false;
  } else if (sent_length != (int)sizeof resp) {
    std::cerr << "ERROR: Incomplete sending of CONACC" << std::endl;
    return false;
  } else {
    // printf("Sent CONACC\n");
  }
  return true;
}

bool send_acc_udp(int socket_fd, struct conn info,
                  struct sockaddr_in client_addr, socklen_t client_addr_len,
                  uint64_t nr_pakietu) {
  struct acc resp;
  resp.id_pakietu = ACC;
  resp.id_sesji = info.id_sesji;
  resp.nr_pakietu = nr_pakietu;
  int sent_length = sendto(socket_fd, &resp, sizeof resp, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
  if (sent_length < 0) {
    std::cerr << "ERROR: Problem with sending ACC, ERRNO: " << errno
              << std::endl;
    return false;
  } else if (sent_length != (int)sizeof resp) {
    std::cerr << "ERROR: Incomplete sending of ACC" << std::endl;
    return false;
  } else {
    // printf("Sent ACC\n");
  }
  return true;
}

bool send_rcvd_udp(int socket_fd, struct conn info,
                   struct sockaddr_in client_addr, socklen_t client_addr_len) {
  struct rcvd resp_rcvd;
  resp_rcvd.id_pakietu = RCVD;
  resp_rcvd.id_sesji = info.id_sesji;
  int sent_length = sendto(socket_fd, &resp_rcvd, sizeof resp_rcvd, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
  if (sent_length < 0) {
    std::cerr << "ERROR: Problem with sending RCVD" << std::endl;
    return false;
  } else if (sent_length != (ssize_t)sizeof resp_rcvd) {
    std::cerr << "ERROR: Incomplete sending of RCVD" << std::endl;
    return false;
  } else {
    // printf("Sent RCVD\n");
  }
  return true;
}

void handle_udp(int socket_fd, struct sockaddr_in client_addr,
                socklen_t client_addr_len, struct conn info) {
  // Sending CONACC
  if (!send_conacc_udp(socket_fd, info, client_addr, client_addr_len)) {
    return;
  }

  int bytes_to_read = info.dlugosc;
  int nr_paczki = 0;
  int HEADER_SIZE = 21;
  char *dane;
  int offset = 0;
  dane = (char *)malloc(sizeof(char) * info.dlugosc);

  char buffer[MAX_PACKAGE_SIZE];
  struct data data;
  while (bytes_to_read > 0) {
    int recieved_length =
        recvfrom(socket_fd, buffer, sizeof buffer, 0,
                 (struct sockaddr *)&client_addr, &client_addr_len);
    if (recieved_length < 0) {
      std::cerr << "ERROR: Problem with reading DATA" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    } else {
      // printf("Recieved %d bytes of DATA\n", recieved_length);
    }

    memcpy(&data, buffer, sizeof data);

    if (data.liczba_bajtow > recieved_length - sizeof data) {
      std::cerr << "ERROR: Too little bytes in a package" << std::endl;
      return;
    }
    if (data.liczba_bajtow < recieved_length - sizeof data) {
      std::cerr << "ERROR: Too many bytes in a package" << std::endl;
      return;
    }
    if (data.id_sesji != info.id_sesji) {
      std::cerr << "ERROR: Wrong session" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }
    if (data.liczba_bajtow == 0) {
      std::cerr << "ERROR: Empty message" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }
    if (data.liczba_bajtow > MAX_PACKAGE_SIZE) {
      std::cerr << "ERROR: Too big message" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }

    if (data.id_pakietu != DATA) {
      std::cerr << "ERROR: Wrong package id" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }
    if (data.nr_pakietu != nr_paczki) {
      std::cerr << "ERROR: Wrong package number. Should get: " << nr_paczki
                << ", got: " << data.nr_pakietu << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }

    memcpy(dane + offset, buffer, recieved_length - sizeof data);

    bytes_to_read -= data.liczba_bajtow;

    offset += data.liczba_bajtow;
    nr_paczki++;
  }

  // Sending RCVD
  if (!send_rcvd_udp(socket_fd, info, client_addr, client_addr_len)) {
    return;
  }

    printf("%s", dane);
}

void handle_udpr(int socket_fd, struct sockaddr_in client_addr,
                 socklen_t client_addr_len, struct conn info) {
  int retransmits = 0;

  // Sending CONACC
  if (!send_conacc_udp(socket_fd, info, client_addr, client_addr_len)) {
    return;
  }

  int bytes_to_read = info.dlugosc;
  int nr_paczki = 0;
  int HEADER_SIZE = 21;
  char *dane;
  int offset = 0;
  dane = (char *)malloc(sizeof(char) * info.dlugosc);

  char buffer[MAX_PACKAGE_SIZE];
  struct data data;
  while (bytes_to_read > 0) {
    int recieved_length =
        recvfrom(socket_fd, buffer, sizeof buffer, 0,
                 (struct sockaddr *)&client_addr, &client_addr_len);
    if (recieved_length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (retransmits >= MAX_RETRANSMITS) {
        std::cerr << "ERROR: Reached maximum number of retransmitts "
                  << std::endl;
        return;
      }
      // std::cerr << "timeout " << std::endl;
      if (nr_paczki == 0) {
        if (!send_conacc_udp(socket_fd, info, client_addr, client_addr_len)) {
          return;
        }
      } else if (!send_acc_udp(socket_fd, info, client_addr, client_addr_len,
                               nr_paczki - 1)) {
        return;
      }
      timeouts ++;
      retransmits++;
      continue;
    } else if (recieved_length < 0) {
      std::cerr << "ERROR: Problem while readning DATA" << std::endl;
    } else if (recieved_length == sizeof(conn)) {
      if (retransmits >= MAX_RETRANSMITS) {
        std::cerr << "ERROR: Reached maximum number of retransmitts "
                  << std::endl;
        return;
      }
      struct conn res;
      memcpy(&res, buffer, sizeof res);
      if (res.id_pakietu == CONN && res.id_sesji == info.id_sesji &&
          res.id_protokolu == UDPR) {
        if (!send_conacc_udp(socket_fd, info, client_addr, client_addr_len)) {
          return;
        }
        timeouts ++;
        retransmits++;
        continue;
      }
    } else {
      memcpy(&data, buffer, sizeof data);

      // printf("Recieved %d bytes of DATA of package %d\n", recieved_length,
            //  data.nr_pakietu);
    }

    if (data.liczba_bajtow > recieved_length - sizeof data) {
      std::cerr << "ERROR: Too little bytes in a package" << std::endl;
      return;
    }
    if (data.liczba_bajtow < recieved_length - sizeof data) {
      std::cerr << "ERROR: Too many bytes in a package" << std::endl;
      return;
    }
    if (data.id_sesji != info.id_sesji) {
      std::cerr << "ERROR: Wrong session" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }
    if (data.liczba_bajtow == 0) {
      std::cerr << "ERROR: Empty message" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }
    if (data.liczba_bajtow > MAX_PACKAGE_SIZE) {
      std::cerr << "ERROR: Too big message" << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }

    // to oznacza zgubionego accepta
    if (data.nr_pakietu == nr_paczki - 1) {
      if (retransmits >= MAX_RETRANSMITS) {
        std::cerr << "ERROR: Reached maximum number of retransmitts "
                  << std::endl;
        return;
      }
      if (!send_acc_udp(socket_fd, info, client_addr, client_addr_len,
                        nr_paczki - 1)) {
        return;
      }
      retransmits++;
      continue;
    }
    if (data.id_pakietu == CONN ||
        (data.id_pakietu == DATA && data.nr_pakietu < nr_paczki)) {
      // std::cerr << "ERROR: Recieved previous package" << std::endl;
      continue;
    }
    if (data.id_pakietu != DATA) {
      std::cerr << "ERROR: Wrong package id " << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }
    if (data.nr_pakietu != nr_paczki) {
      std::cerr << "ERROR: Wrong package number. Should get: " << nr_paczki
                << ", got: " << data.nr_pakietu << std::endl;
      send_rjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji,
                   nr_paczki);
      return;
    }

    memcpy(dane + offset, buffer, recieved_length - sizeof data);

    bytes_to_read -= data.liczba_bajtow;

    if (!send_acc_udp(socket_fd, info, client_addr, client_addr_len,
                      nr_paczki)) {
      return;
    }

    offset += data.liczba_bajtow;
    nr_paczki++;
    retransmits = 0;
  }

  // Sending RCVD
  if (!send_rcvd_udp(socket_fd, info, client_addr, client_addr_len)) {
    return;
  }

  printf("%s", dane);
}

void switch_udp(int socket_fd, struct sockaddr_in client_addr,
                socklen_t client_addr_len, bool retransmittion) {
  char const *client_ip = inet_ntoa(client_addr.sin_addr);
  uint16_t client_port = ntohs(client_addr.sin_port);

  struct timeval tv;
  tv.tv_sec = MAX_WAIT;
  tv.tv_usec = 0;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("Error");
  }

  int recieved_length = -1;
  int sent_length;
  // Reading CONN
  struct conn info;
  errno = EAGAIN;
  while (recieved_length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    recieved_length =
        recvfrom(socket_fd, &info, sizeof info, 0,
                 (struct sockaddr *)&client_addr, &client_addr_len);
  }

  if (recieved_length < 0) {
    std::cerr << "ERROR: Problem with reading CONN " << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return;
  } else {
    // printf("Recieved CONN\n");
  }

  if (info.id_pakietu != CONN) {
    std::cerr << "ERROR: Wrong package id" << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return;
  }
  if (info.dlugosc == 0) {
    std::cerr << "ERROR: Empty message" << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return;
  }
  if (info.id_protokolu == UDP && !retransmittion) {
    handle_udp(socket_fd, client_addr, client_addr_len, info);
  } else if (info.id_protokolu == UDPR) {
    handle_udpr(socket_fd, client_addr, client_addr_len, info);
  } else {
    std::cerr << "ERROR: Wrong protocol id" << std::endl;
    send_conrjt_udp(socket_fd, client_addr, client_addr_len, info.id_sesji);
    return;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <tcp/udp/udpr> <port>" << std::endl;
    return 1;
  }

  const char *protocol = argv[1];
  int port = std::stoi(argv[2]);
  int socket_fd = socket(
      AF_INET, (strcmp(protocol, "tcp") == 0) ? SOCK_STREAM : SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    std::cerr << "ERROR: Cannot create socket" << std::endl;
    return 1;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    std::cerr << "ERROR: Cannot bind socket" << std::endl;
    return 1;
  }

  if (strcmp(protocol, "tcp") == 0) {
    listen(socket_fd, 1);
    while (true) {
      timeouts = 0;
      struct sockaddr_in client_addr;
      socklen_t client_addr_len = sizeof(client_addr);
      printf("listening on port %" PRIu16 "\n", port);
      int client_socket =
          accept(socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);

      if (client_socket < 0) {
        std::cerr << "ERROR: Cannot accept connection" << std::endl;
        continue;
      }
      clock_t begin = clock();
      handle_tcp(client_socket, client_addr);
      clock_t end = clock();
      close(client_socket);

      double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
      std::cout << "Time it took to recieve message: " << elapsed_secs << std::endl;
      std::cout << "Number of timeouts (retransmitts, lost packages) " << timeouts << std::endl;

    }
  } else if (strcmp(protocol, "udp") == 0) {
    while (true) {
      timeouts = 0;
      struct sockaddr_in client_addr;
      socklen_t client_addr_len = (socklen_t)sizeof(client_addr);
      printf("listening on port %" PRIu16 "\n", port);
      clock_t begin = clock();
      switch_udp(socket_fd, client_addr, client_addr_len, false);
      clock_t end = clock();
      double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
      std::cout << "time it took to recieve message: " << elapsed_secs << std::endl;
      std::cout << "Number of timeouts (retransmitts, lost packages) " << timeouts << std::endl;
    }
  } else {
    while (true) {
      timeouts = 0;
      struct sockaddr_in client_addr;
      socklen_t client_addr_len = (socklen_t)sizeof(client_addr);
      printf("listening on port %" PRIu16 "\n", port);
      clock_t begin = clock();
      switch_udp(socket_fd, client_addr, client_addr_len, true);
      clock_t end = clock();
      double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
      std::cout << "time it took to recieve message: " << elapsed_secs << std::endl;
      std::cout << "Number of timeouts (retransmitts, lost packages) " << timeouts << std::endl;
    }
  }

  close(socket_fd);
  return 0;
}
