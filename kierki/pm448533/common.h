#ifndef COMMON_H
#define COMMON_H

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>
#include <regex>
#include <sstream>

#define BUFFER_SIZE 512
#define TIMEOUT 10
#define QUEUE_LENGTH 1024
#define CONNECTIONS 1024
#define MAX_MESSAGE_LEN 350

std::string convertToString(char* a, int size);
struct sockaddr_in get_server_address(char const* host, uint16_t port);
uint16_t read_port(char const* string);
bool is_it_a_card(std::string card);
std::string getTime();

enum steps { UNBORN, IAM, DEAL, TRICK, GIVE, TAKEN, SCORE, TOTAL, DEAD };

class Poll_handler {
 public:
  struct pollfd* pfd;
  char read_buffer[BUFFER_SIZE];
  char write_buffer[BUFFER_SIZE];
  int read_pointer = 0;
  int write_pointer = 0;
  int write_length = 0;
  const char* ip;
  steps step = UNBORN;
  bool debug = false;
  time_t start;

  Poll_handler();
  Poll_handler(struct pollfd* pfd);
  void act(int* active_clients, int* places);
  bool is_active();
  void shift_read_buffer(int sh);
  void shift_write_buffer(int sh);
  void wrote(std::string s);
  void listen();
  bool check();
  void close_handler();
  void kill();
  void clear_read_buffer();
  void clean();
  bool is_too_late(int timeout);
  void start_time();
};

#endif  // COMMON_H