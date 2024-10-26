#include "common.h"

std::string convertToString(char *a, int size) {
  int i;
  std::string s = "";
  for (i = 0; i < size; i++) {
    s += a[i];
  }
  return s;
}

uint16_t read_port(char const *string) {
  char *endptr;
  errno = 0;
  unsigned long port = strtoul(string, &endptr, 10);
  if (errno != 0 || *endptr != 0 || port > UINT16_MAX) {
    std::cerr << "Error: " << string << "is not a valid port number"
              << std::endl;
  }
  return (uint16_t)port;
}

std::string getTime() {
  timeval curTime;
  gettimeofday(&curTime, NULL);

  int milli = curTime.tv_usec / 1000;
  char buf[sizeof "2011-10-08T07:07:09.000"];
  char *p = buf + strftime(buf, sizeof buf, "%FT%T", gmtime(&curTime.tv_sec));
  sprintf(p, ".%d", milli);

  return buf;
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
    std::cerr << "Error: Problem with getting address info" << std::endl;
  }

  struct sockaddr_in send_address;
  send_address.sin_family = AF_INET;  // IPv4
  send_address.sin_addr.s_addr =      // IP address
      ((struct sockaddr_in *)(address_result->ai_addr))->sin_addr.s_addr;
  send_address.sin_port = htons(port);  // port from the command line

  freeaddrinfo(address_result);

  return send_address;
}

Poll_handler::Poll_handler()
    : pfd(NULL), read_pointer(0), write_pointer(0), write_length(0) {}

Poll_handler::Poll_handler(struct pollfd *pfd)
    : pfd(pfd), read_pointer(0), write_pointer(0), write_length(0) {}

void Poll_handler::act(int *active_clients, int *places) {
  if (pfd->fd != -1 && (pfd->revents & (POLLIN | POLLERR))) {
    int received_bytes = read(pfd->fd, read_buffer + read_pointer, BUFFER_SIZE);
    if (received_bytes < 0) {
      std::cerr << "Error: Problem with reading message." << std::endl;
      close_handler();
      (*active_clients)--;
      (*places)--;
    } else if (received_bytes == 0) {
      if(debug){
        std::cerr << "Error: Read returnted 0. Ending connection." << std::endl;
      }
      close_handler();
      (*active_clients)--;
      (*places)--;
    } else {
      read_pointer += received_bytes;
      if (debug) {
        printf("\nRead %d bytes\n", received_bytes);
      }
    }
  }
  if (pfd->fd != -1 && (pfd->revents & POLLOUT) && write_length > 0) {
    int written_bytes =
        write(pfd->fd, write_buffer + write_pointer, write_length);
    if (written_bytes < 0) {
      std::cerr << "Error: Problem with writing message." << std::endl;
      close_handler();
      (*active_clients)--;
      (*places)--;
    } else if (written_bytes == 0) {
      if(debug){
        std::cerr << "Error: Write returned 0. Ending connection. " << std::endl;
      }
      close_handler();
      (*active_clients)--;
      (*places)--;
    } else {
      write_length -= written_bytes;
      if (write_length == 0) {
        write_pointer = 0;
        pfd->events = POLLIN | POLLOUT;
      } else {
        write_pointer += written_bytes;
      }
      if (debug) {
        printf("\nWrote %d bytes\n", written_bytes);
      }
    }
  }
}

bool Poll_handler::check() {
  char buf[8];
  ssize_t rec = recv(pfd->fd, buf, 1, MSG_PEEK);
  if (rec == 0) return false;
  return true;
}

bool Poll_handler::is_active() { return pfd->fd != -1; }

void Poll_handler::shift_read_buffer(int sh) {
  if (sh == read_pointer) read_pointer = 0;
  std::string s = convertToString(read_buffer + sh, read_pointer - sh);
  memcpy(read_buffer, s.c_str(), s.size());
  read_pointer = s.size();
}

void Poll_handler::shift_write_buffer(int sh) {
  if (sh > write_length + write_pointer) sh = write_length + write_pointer;
  std::string s =
      convertToString(write_buffer + sh, write_length + write_pointer - sh);
  memcpy(write_buffer, s.c_str(), s.size());
  if (sh <= write_pointer) {
    write_pointer -= sh;
  } else {
    sh -= write_pointer;
    write_pointer = 0;
    write_length -= sh;
  }
}

void Poll_handler::wrote(std::string s) {
  memcpy(write_buffer + write_length + write_pointer, s.c_str(), s.size());
  write_length += s.size();
  pfd->events = POLLIN | POLLOUT;
}

void Poll_handler::listen() { pfd->events = POLLIN; }

void Poll_handler::close_handler() {
  close(pfd->fd);
  step = DEAD;
  pfd->fd = -1;
}

void Poll_handler::kill() { step = DEAD; }

void Poll_handler::clear_read_buffer() { read_pointer = 0; }

bool is_it_a_card(std::string card) {
  std::regex cardPattern(R"((2|3|4|5|6|7|8|9|10|J|Q|K|A)(C|D|H|S))");
  if (!std::regex_match(card, cardPattern)) {
    return false;
  }
  return true;
}

void Poll_handler::clean() {
  if (read_pointer > MAX_MESSAGE_LEN) {
    int sh = read_pointer - MAX_MESSAGE_LEN;
    shift_read_buffer(sh);
  }
  if (write_pointer > 0) {
    shift_write_buffer(write_pointer);
  }
  if (write_length > MAX_MESSAGE_LEN) {
    int sh = write_length - MAX_MESSAGE_LEN;
    shift_write_buffer(sh);
  }
}

bool Poll_handler::is_too_late(int timeout) {
  time_t ending;
  time(&ending);
  if (difftime(ending, start) > timeout) {
    return true;
  }
  return false;
}

void Poll_handler::start_time() { time(&start); }