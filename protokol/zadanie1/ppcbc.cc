#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
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
int timeouts = 0;


uint16_t read_port(char const *string) {
  char *endptr;
  unsigned long port = strtoul(string, &endptr, 10);
  if ((port == ULONG_MAX && errno == ERANGE) || *endptr != 0 || port == 0 ||
      port > UINT16_MAX) {
    fatal("%s is not a valid port number", string);
  }
  return (uint16_t)port;
}

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

unsigned rand256() {
  static unsigned const limit = RAND_MAX - RAND_MAX % 256;
  unsigned result = rand();
  while (result >= limit) {
    result = rand();
  }
  return result % 256;
}

unsigned long long rand64bits() {
  unsigned long long results = 0ULL;
  for (int count = 8; count > 0; --count) {
    results = 256U * results + rand256();
  }
  return results;
}

bool send_conn_tcp(int socket_fd, const char *dane, struct conn info) {
  int written_length;
  // Sending CONN

  written_length = writen(socket_fd, &info, sizeof info);
  if ((size_t)written_length < sizeof info) {
    std::cerr << "ERROR: Problem while writing CONN message" << std::endl;
    return false;
  } else {
    // printf("Sent CONN\n");
  }
  return true;
}

bool recieve_conacc_tcp(int socket_fd, struct conn info) {
  int read_length;
  struct conacc resp;
  read_length = readn(socket_fd, &resp, sizeof resp);
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
  } else if ((size_t)read_length < sizeof resp) {
    std::cerr
        << "ERROR: Connection closed without providing full data structure"
        << std::endl;
    return false;
  } else {
    // printf("recieved CONACC\n");
  }

  if (resp.id_pakietu == CONRJT) {
    std::cerr << "ERROR: Recieved CONRJT message" << std::endl;
    return false;
  } else if (resp.id_pakietu != CONACC) {
    std::cerr << "ERROR: Wrong package id" << std::endl;
    return false;
  }
  if (resp.id_sesji != info.id_sesji) {
    std::cerr << "ERROR: Wrong session number" << std::endl;
    return false;
  }
  return true;
}

bool recieve_rcvd_tcp(int socket_fd, struct conn info) {
  struct rcvd resp_rcvd;
  int read_length = readn(socket_fd, &resp_rcvd, sizeof resp_rcvd);
  if (read_length < 0) {
    if (errno == EAGAIN) {
      // std::cerr << "ERROR: Timeout" << std::endl;
      timeouts ++;
      return false;
    } else {
      std::cerr << "ERROR: Problem while reading" << std::endl;
      return false;
    }
  } else if (read_length == 0) {
    std::cerr << "ERROR: Connection closed" << std::endl;
    return false;
  } else if ((size_t)read_length < sizeof resp_rcvd) {
    std::cerr
        << "ERROR: Connection closed without providing full data structure"
        << std::endl;
    return false;
  } else {
    // printf("recieved RCVD\n");
  }

  if (resp_rcvd.id_pakietu == RJT) {
    std::cerr << "ERROR: Recieved RJT message" << std::endl;
    return false;
  } else if (resp_rcvd.id_pakietu != RCVD) {
    std::cerr << "ERROR: Wrong package id" << std::endl;
    return false;
  }
  if (resp_rcvd.id_sesji != info.id_sesji) {
    std::cerr << "ERROR: Wrong session number" << std::endl;
    return false;
  }
  return true;
}

int handle_tcp(int socket_fd, const char *dane) {
  struct timeval tv;
  tv.tv_sec = MAX_WAIT;
  tv.tv_usec = 0;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("Error");
  }
  
  struct conn info;
  info.id_pakietu = CONN;
  info.id_sesji = rand64bits();
  info.id_protokolu = TCP;
  info.dlugosc = strlen(dane);
  // Sending CONN
  if (!send_conn_tcp(socket_fd, dane, info)) {
    return 1;
  }

  // Recieving CONACC
  if (!recieve_conacc_tcp(socket_fd, info)) {
    return 1;
  }

  int bytes_to_read = info.dlugosc;
  int offset = 0;
  int pack;
  int nr_pakietu = 0;
  while (bytes_to_read > 0) {
    // Sending DATA HEADER
    if (bytes_to_read > MAX_PACKAGE_SIZE) {
      bytes_to_read -= MAX_PACKAGE_SIZE;
      pack = MAX_PACKAGE_SIZE;
    } else {
      pack = bytes_to_read;
      bytes_to_read = 0;
    }
    struct data data;
    data.id_pakietu = DATA;
    data.id_sesji = info.id_sesji;
    data.liczba_bajtow = pack;
    data.nr_pakietu = nr_pakietu;
    int written_length = writen(socket_fd, &data, sizeof(data));
    if ((size_t)written_length < sizeof data) {
      std::cerr << "ERROR: Problem while writing DATA HEADER" << std::endl;
      return 1;
    } else {
      // printf("Sent DATA HEADER\n");
    }

    // Sending DATA
    written_length = writen(socket_fd, dane + offset, pack);
    if ((size_t)written_length < pack) {
      std::cerr << "ERROR: Problem while writing DATA" << std::endl;
      return 1;
    } else {
      // printf("Sent DATA\n");
    }
    offset += pack;
    nr_pakietu++;
  }

  // Recieving RCVD
  if (!recieve_rcvd_tcp(socket_fd, info)) {
    return 1;
  }
  return 0;
}

bool send_conn_udp(int socket_fd, sockaddr_in client_addr,
                   socklen_t client_addr_len, const char *dane,
                   struct conn info) {
  int sent_length = sendto(socket_fd, &info, sizeof info, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
  if (sent_length < 0) {
    std::cerr << "ERROR: Problem with sending CONN" << std::endl;
    return false;
  } else if (sent_length != (ssize_t)sizeof info) {
    std::cerr << "ERROR: Incomplete sending of CONN" << std::endl;
    return false;
  } else {
    // printf("Sent CONN\n");
  }
  return true;
}

int recieve_conacc_udp(int socket_fd, sockaddr_in client_addr,
                       socklen_t client_addr_len, struct conn info) {
  struct conacc resp;
  int received_length =
      recvfrom(socket_fd, &resp, sizeof resp, 0,
               (struct sockaddr *)&client_addr, &client_addr_len);
  if (received_length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // std::cerr << "ERROR: Reached timeout" << std::endl;
    timeouts ++;
    return 2;
  } else if (received_length < 0) {
    std::cerr << "ERROR: Problem with reading CONACC" << std::endl;
    return 1;
  } else {
    // printf("Recieved CONACC\n");
  }

  if (resp.id_pakietu == CONRJT) {
    std::cerr << "ERROR: Recieved CONRJT message" << std::endl;
    return 1;
  } else if (resp.id_pakietu != CONACC) {
    std::cerr << "ERROR: Wrong package id" << std::endl;
    return 1;
  }
  if (resp.id_sesji != info.id_sesji) {
    std::cerr << "ERROR: Wrong session number" << std::endl;
    return 1;
  }
  return 0;
}

int recieve_acc_udp(int socket_fd, sockaddr_in client_addr,
                    socklen_t client_addr_len, struct conn info,
                    uint64_t nr_paczki) {
  struct acc resp;
  int received_length =
      recvfrom(socket_fd, &resp, sizeof resp, 0,
               (struct sockaddr *)&client_addr, &client_addr_len);
  if (received_length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    timeouts ++;
    // std::cerr << "ERROR: Reached timeout" << std::endl;
    return 1;
  } else if (received_length < 0) {
    std::cerr << "ERROR: Problem with reading ACC" << std::endl;
    return 1;
  } else {
    // printf("Recieved ACC\n");
  }

  if (resp.id_pakietu == RJT) {
    std::cerr << "ERROR: Recieved RJT message" << std::endl;
    return 2;
  }
  if (resp.id_pakietu == CONACC ||
      (resp.id_pakietu == ACC && resp.nr_pakietu < nr_paczki)) {
    // std::cerr << "ERROR: Recieved previous package" << std::endl;
    return 1;
  }
  if (resp.id_pakietu != ACC) {
    // std::cerr << "ERROR: Wrong package id" << std::endl;
    return 1;
  }
  if (resp.id_sesji != info.id_sesji) {
    // std::cerr << "ERROR: Wrong session number" << std::endl;
    return 1;
  }
  if (resp.nr_pakietu != nr_paczki) {
    // std::cerr << "ERROR: Wrong package number" << std::endl;
    return 1;
  }
  return 0;
}

bool recieve_rcvd_udp(int socket_fd, sockaddr_in client_addr,
                      socklen_t client_addr_len, struct conn info) {
  struct rcvd resp_rcvd;
  int received_length =
      recvfrom(socket_fd, &resp_rcvd, sizeof resp_rcvd, 0,
               (struct sockaddr *)&client_addr, &client_addr_len);
  if (received_length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // std::cerr << "ERROR: Reached timeout" << std::endl;
    timeouts ++;
    return false;
  } else if (received_length < 0) {
    std::cerr << "ERROR: Problem with reading RCVD " << std::endl;
    return false;
  } else {
    // printf("Recieved RCVD\n");
  }

  if (resp_rcvd.id_pakietu == RJT) {
    std::cerr << "ERROR: Recieved RJT message" << std::endl;
    return false;
  } else if (resp_rcvd.id_pakietu != RCVD) {
    std::cerr << "ERROR: Wrong package id" << std::endl;
    return false;
  }
  if (resp_rcvd.id_sesji != info.id_sesji) {
    std::cerr << "ERROR: Wrong session number" << std::endl;
    return false;
  }
  return true;
}

int handle_udp(int socket_fd, sockaddr_in client_addr,
               socklen_t client_addr_len, const char *dane) {
  struct conn info;
  info.id_pakietu = CONN;
  info.id_protokolu = UDP;
  info.dlugosc = strlen(dane);
  info.id_sesji = rand64bits();
  // Sending CONN
  if (!send_conn_udp(socket_fd, client_addr, client_addr_len, dane, info)) {
    return 1;
  }

  // Recieving CONACC
  if (recieve_conacc_udp(socket_fd, client_addr, client_addr_len, info) != 0) {
    return 1;
  }

  int bytes_to_send = info.dlugosc;
  int offset = 0;
  int pack;
  int nr_pakietu = 0;
  char buffer[MAX_PACKAGE_SIZE];
  while (bytes_to_send > 0) {
    struct data data;
    data.id_pakietu = DATA;
    data.id_sesji = info.id_sesji;
    data.nr_pakietu = nr_pakietu;
    // Sending DATA HEADER
    if (bytes_to_send > MAX_PACKAGE_SIZE - sizeof data) {
      bytes_to_send -= (MAX_PACKAGE_SIZE - sizeof data);
      pack = MAX_PACKAGE_SIZE - sizeof data;
    } else {
      pack = bytes_to_send;
      bytes_to_send = 0;
    }
    data.liczba_bajtow = pack;

    memcpy(buffer, &data, sizeof data);
    memcpy(buffer + sizeof data, dane + offset, pack);

    // Sending DATA HEADER + DATA
    int sent_length = sendto(socket_fd, buffer, pack + sizeof data, 0,
                             (struct sockaddr *)&client_addr, client_addr_len);
    if (sent_length < 0) {
      std::cerr << "ERROR: Problem with sending DATA" << std::endl;
      return 1;
    } else if (sent_length != pack + sizeof data) {
      std::cerr << "ERROR: Incomplete sending of DATA sent_length, pack: "
                << std::endl;
      return 1;
    } else {
      // printf("Sent DATA HEADER +  DATA in package nr: %ld\n", data.nr_pakietu);
    }

    offset += pack;
    nr_pakietu++;
  }

  // Recieving RCVD
  if (!recieve_rcvd_udp(socket_fd, client_addr, client_addr_len, info)) {
    return 1;
  }
  return 0;
}

int handle_udpr(int socket_fd, sockaddr_in client_addr,
                socklen_t client_addr_len, const char *dane) {
  struct timeval tv;
  tv.tv_sec = MAX_WAIT;
  tv.tv_usec = 0;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("Error");
  }
  int retransmits = 0;

  struct conn info;
  info.id_pakietu = CONN;
  info.id_protokolu = UDPR;
  info.dlugosc = strlen(dane);
  info.id_sesji = rand64bits();
  // Sending CONN
  if (!send_conn_udp(socket_fd, client_addr, client_addr_len, dane, info)) {
    return 1;
  }

  // Recieving CONACC
  int retry = 0;
  int response =
      recieve_conacc_udp(socket_fd, client_addr, client_addr_len, info);
  while (response == 2 && retry < MAX_RETRANSMITS) {
    if (!send_conn_udp(socket_fd, client_addr, client_addr_len, dane, info)) {
      return 1;
    }
    response =
        recieve_conacc_udp(socket_fd, client_addr, client_addr_len, info);
    retry++;
  }
  if (response == 1) {
    return 1;
  } else if (response == 2) {
    std::cerr << "ERROR: Reached max number of retransmits while sending CONN"
              << std::endl;
    return 1;
  }

  int bytes_to_send = info.dlugosc;
  int offset = 0;
  int pack;
  int nr_pakietu = 0;
  char buffer[MAX_PACKAGE_SIZE];
  bool recieved;
  int recieved_message;
  while (bytes_to_send > 0) {
    struct data data;
    data.id_pakietu = DATA;
    data.id_sesji = info.id_sesji;
    data.nr_pakietu = nr_pakietu;
    if (bytes_to_send > MAX_PACKAGE_SIZE - sizeof data) {
      bytes_to_send -= (MAX_PACKAGE_SIZE - sizeof data);
      pack = MAX_PACKAGE_SIZE - sizeof data;
    } else {
      pack = bytes_to_send;
      bytes_to_send = 0;
    }
    data.liczba_bajtow = pack;

    memcpy(buffer, &data, sizeof data);
    memcpy(buffer + sizeof data, dane + offset, pack);

    recieved = false;
    retransmits = 0;
    do {
      // Sending DATA HEADER + DATA
      int sent_length =
          sendto(socket_fd, buffer, pack + sizeof data, 0,
                 (struct sockaddr *)&client_addr, client_addr_len);
      if (sent_length < 0) {
        std::cerr << "ERROR: Problem with sending DATA" << std::endl;
        return 1;
      } else if (sent_length != pack + sizeof data) {
        std::cerr << "ERROR: Incomplete sending of DATA sent_length, pack: "
                  << std::endl;
        return 1;
      } else {
        // printf("Sent DATA HEADER +  DATA in package nr: %ld\n",
              //  data.nr_pakietu);
      }

      retransmits++;
      recieved_message = recieve_acc_udp(socket_fd, client_addr,
                                         client_addr_len, info, nr_pakietu);
      if (recieved_message == 0) {
        recieved = true;
      } else if (recieved_message == 2) {
        return 1;
      }

    } while (!recieved && retransmits < MAX_RETRANSMITS);
    if (!recieved) {
      std::cerr << "ERROR: Reached max number of retransmitts" << std::endl;
      return 1;
    }

    offset += pack;
    nr_pakietu++;
  }

  // Recieving RCVD
  if (!recieve_rcvd_udp(socket_fd, client_addr, client_addr_len, info)) {
    return 1;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <tcp/udp> <server_ip> <port>"
              << std::endl;
    return 1;
  }

  const char *protocol = argv[1];
  const char *server_ip = argv[2];
  int port = read_port(argv[3]);
  std::string tmp;
  char ch = getchar();
  while (ch != EOF) {
    tmp += ch;
    ch = getchar();
  }

  static char *dane = new char[tmp.size()];
  strcpy(dane, tmp.c_str());

  struct sockaddr_in server_addr = get_server_address(server_ip, port);
  socklen_t address_length = (socklen_t)sizeof(server_addr);

  int socket_fd = socket(
      AF_INET, (strcmp(protocol, "tcp") == 0) ? SOCK_STREAM : SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    std::cerr << "ERROR: Cannot create socket" << std::endl;
    return 1;
  }

  int return_statement;
  clock_t begin = clock();
  if (strcmp(protocol, "tcp") == 0) {
    if (connect(socket_fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
      std::cerr << "ERROR: Cannot connect to server" << std::endl;
      return 1;
    }
    return_statement = handle_tcp(socket_fd, dane);
  } else if (strcmp(protocol, "udp") == 0) {
    return_statement = handle_udp(socket_fd, server_addr, address_length, dane);
  } else {
    return_statement =
        handle_udpr(socket_fd, server_addr, address_length, dane);
  }
  clock_t end = clock();

  close(socket_fd);
  double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
  std::cout << "Time it took to send messages: " << elapsed_secs << std::endl;
  std::cout << "Number of timeouts (retransmitts, lost packages) " << timeouts << std::endl;


  return return_statement;
}
