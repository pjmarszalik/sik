#include "Client.hpp"

Client::Client(const char* host, uint16_t port, const std::string& position,
               bool is_automated, int ip_version)
    : host(host),
      port(port),
      position(position),
      is_automated(is_automated),
      ip_version(ip_version),
      client_socket(-1) {}

bool Client::run() {
  if (connect_to_server()) {
    if (!play_game()) {
      return false;
    }
  } else {
    return false;
  }
  close(client_socket);
  return true;
}

bool Client::connect_to_server() {
  struct sockaddr_in server_address = get_server_address(host, port);

  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket < 0) {
    if (debug) {
      std::cerr << "Error: could not create socket." << std::endl;
    }
    return false;
  }

  if (connect(client_socket, (struct sockaddr*)&server_address,
              (socklen_t)sizeof(server_address)) < 0) {
    if (debug) {
      std::cerr << "Error: could not connect to server." << std::endl;
    }
    return false;
  }

  fcntl(client_socket, F_SETFL, O_NONBLOCK);
  get_ips();
  return true;
}

bool Client::play_game() {
  struct pollfd poll_descriptors[connections];

  // The main socket has index 0.
  poll_descriptors[0].fd = client_socket;
  // step = IAM;
  poll_descriptors[0].events = POLLIN;

  for (int i = 1; i < connections; ++i) {
    poll_descriptors[i].fd = -1;
    poll_descriptors[i].events = POLLIN;
  }
  int active_clients = 3;
  bool finish = false;

  poll_descriptors[1].fd = 0;
  poll_descriptors[1].events = POLLIN;
  poll_descriptors[2].fd = 1;
  poll_descriptors[2].events = POLLIN;

  poll_handlers[0] = Poll_handler(&poll_descriptors[0]);
  poll_handlers[1] = Poll_handler(&poll_descriptors[1]);
  poll_handlers[2] = Poll_handler(&poll_descriptors[2]);
  poll_handlers[0].step = IAM;
  poll_handlers[1].step = IAM;
  poll_handlers[2].step = IAM;

  std::string iam_message = "IAM" + position + "\r\n";
  // step = DEAL;
  poll_handlers[0].wrote(iam_message);
  sent(iam_message);
  do {
    for (int i = 0; i < connections; ++i) {
      poll_descriptors[i].revents = 0;
    }

    // After Ctrl-C the main socket is closed.
    if (finish && poll_descriptors[0].fd >= 0) {
      close(poll_descriptors[0].fd);
      poll_descriptors[0].fd = -1;
    }

    int poll_status = poll(poll_descriptors, connections, TIMEOUT);
    if (poll_status == -1) {
      if (errno == EINTR) {
        if (debug) {
          std::cerr << "Error: Interrupted system call." << std::endl;
        }
        return false;
      } else {
        if (debug) {
          std::cerr << "Error: Problem with executing poll." << std::endl;
        }
        return false;
      }
    } else if (poll_status > 0) {
    } else {
      if (debug) {
        std::cout << TIMEOUT << " milliseconds passed without any events\n";
      }
    }

    // Serve data connections.
    int nop;
    for (int i = 0; i < connections; ++i) {
      if (poll_handlers[i].step == UNBORN) {
        continue;
      }
      poll_handlers[i].act(&active_clients, &nop);
      poll_handlers[i].clean();
    }

    if (poll_handlers[0].step == DEAD && poll_handlers[0].pfd->fd != -1) {
      poll_handlers[0].close_handler();
      active_clients--;
    }

    for (int i = 0; i < connections; i++) {
      if (poll_handlers[i].step == UNBORN) {
        continue;
      }
      if (i == 1) {
        std::string buffer = convertToString(poll_handlers[i].read_buffer,
                                             poll_handlers[i].read_pointer);
        size_t pos1 = buffer.find("!");
        size_t pos2 = buffer.find("cards");
        size_t pos3 = buffer.find("tricks");
        if (pos1 < pos2 && pos1 < pos3) {
          read_card(buffer, i);
        } else if (pos2 < pos1 && pos2 < pos3) {
          read_cards(buffer, i);
        } else if (pos3 < pos2 && pos3 < pos1) {
          read_tricks(buffer, i);
        }
      } else if (i == 2) {
      } else if (poll_handlers[i].pfd->revents & (POLLIN | POLLERR) ||
                 poll_handlers[i].read_pointer > 0) {
        std::string buffer = convertToString(poll_handlers[i].read_buffer,
                                             poll_handlers[i].read_pointer);
        if (debug) {
          std::cerr << buffer.size() << std::endl;
        }
        size_t pos = buffer.find("\r\n");
        if (pos != std::string::npos) {
          std::string message = buffer.substr(0, pos);
          recieved(buffer.substr(0, pos + 2));
          poll_handlers[i].shift_read_buffer(pos + 2);
          std::string output = "";
          if (message.find("DEAL", 0) == 0) {
            output = handle_deal(message);
          } else if (message.find("TRICK", 0) == 0) {
            std::pair<std::string, std::string> ans;
            ans = handle_trick(message);
            output = ans.second;
            if (is_automated) {
              poll_handlers[i].wrote(ans.first);
              sent(ans.first);
            }
          } else if (message.find("TAKEN", 0) == 0) {
            output = handle_taken(message);
          } else if (message.find("BUSY", 0) == 0) {
            output = handle_busy(message);
            poll_handlers[i].step = DEAD;

          } else if (message.find("WRONG", 0) == 0) {
            output = handle_wrong(message);
          } else if (message.find("SCORE", 0) == 0) {
            output = handle_score(message);
          } else if (message.find("TOTAL", 0) == 0) {
            output = handle_total(message);
          }
          if (output.size() > 0 && !is_automated) {
            poll_handlers[2].wrote(output);
          }
        }
      }
      if (poll_handlers[i].pfd->revents & POLLOUT) {
      }
    }

    if (poll_descriptors[0].fd == -1 && poll_handlers[2].write_length == 0 &&
        poll_handlers[0].read_pointer == 0)
      finish = true;
  } while (!finish && active_clients > 0);

  return true;
}

void Client::read_card(std::string buffer, int i) {
  if (debug) {
    std::cerr << "read_card\n";
  }
  size_t pos = buffer.find("!");
  if (pos == std::string::npos) return;
  buffer = buffer.substr(pos + 1);
  if (buffer.size() < 3) {
    return;
  }
  std::string card = buffer.substr(0, 2);
  if (!is_it_a_card(card)) card = buffer.substr(0, 3);
  if (!is_it_a_card(card)) {
    poll_handlers[i].shift_read_buffer(pos + card.size());
    return;
  }

  poll_handlers[i].shift_read_buffer(pos + card.size());

  if (remove_card(card)) {
    std::string mess = "TRICK" + std::to_string(lew_number) + card + "\r\n";
    poll_handlers[0].wrote(mess);
    sent(mess);
    last_card = card;
  }
}

void Client::read_cards(std::string buffer, int i) {
  if (debug) {
    std::cerr << "read_cards\n";
  }
  std::string ans;
  size_t pos = buffer.find("cards");
  if (pos == std::string::npos) return;
  for (int i = 0; i < (int)hand.size(); i++) {
    ans = ans + hand[i];
    if (i < (int)hand.size() - 1) {
      ans = ans + ", ";
    } else {
      ans = ans + "\n";
    }
  }
  poll_handlers[i].shift_read_buffer(pos + 5);
  poll_handlers[2].wrote(ans);
}

void Client::read_tricks(std::string buffer, int i) {
  if (debug) {
    std::cerr << "read_tricks\n";
  }
  std::string ans;
  size_t pos = buffer.find("tricks");
  if (pos == std::string::npos) return;
  for (int i = 0; i < lew_number - 1; i++) {
    for (int j = 0; j < 4; j++) {
      ans = ans + tricks_taken[i][j];
      if (j < 3) {
        ans = ans + ", ";
      } else {
        ans = ans + "\n";
      }
    }
  }
  poll_handlers[i].shift_read_buffer(pos + 6);
  poll_handlers[2].wrote(ans);
}

std::string Client::handle_deal(const std::string& message) {
  if (debug) {
    std::cerr << "handle_deal\n";
  }
  if (!is_valid_deal_message(message) /*|| (step != SCORE && step != DEAL) */) {
    return "";
  }
  hand.clear();
  game_type = (int)(message[4] - '0');
  next_player = message[5];
  int i = 6;
  while (i < (int)message.size()) {
    if (message[i] == '1') {
      hand.push_back(message.substr(i, 3));
      i += 3;
    } else {
      hand.push_back(message.substr(i, 2));
      i += 2;
    }
  }
  std::string output = "New deal " + std::to_string(game_type) +
                       ": starting place " + next_player + ", your cards: ";
  for (int i = 0; i < (int)hand.size(); i++) {
    output = output + hand[i];
    if (i < (int)hand.size() - 1) {
      output = output + ", ";
    } else {
      output = output + ".";
    }
  }
  // step = TRICK;
  return output;
}

std::pair<std::string, std::string> Client::handle_trick(
    const std::string& message) {
  if (debug) {
    std::cerr << "handle_trick\n";
  }
  if (!is_valid_trick_message(message) /*|| step != TRICK*/) {
    return {"", ""};
  }
  int i;
  std::string lew;
  if (is_it_a_card(message.substr(6, 2)) ||
      is_it_a_card(message.substr(6, 3))) {
    i = 6;
    lew = message.substr(5, 1);
  } else {
    i = 7;
    lew = message.substr(5, 2);
  }
  int lew_nr = std::stoi(lew);
  if (lew_nr != lew_number) return {"", ""};
  cards_on_table.clear();
  while (i < (int)message.size()) {
    if (message[i] == '1') {
      cards_on_table.push_back(message.substr(i, 3));
      i += 3;
    } else {
      cards_on_table.push_back(message.substr(i, 2));
      i += 2;
    }
  }
  std::pair<std::string, std::string> output;

  std::string ans = "";

  ans = ans + "Trick: (" + std::to_string(lew_number) + ") ";
  for (int i = 0; i < (int)cards_on_table.size(); i++) {
    ans = ans + cards_on_table[i];
    if (i < (int)cards_on_table.size() - 1) {
      ans = ans + ", ";
    } else {
      ans = ans + "\n";
    }
  }

  ans = ans + "Available: ";
  for (int i = 0; i < (int)hand.size(); i++) {
    ans = ans + hand[i];
    if (i < (int)hand.size() - 1) {
      ans = ans + ", ";
    } else {
      ans = ans + "\n";
    }
  }
  output.second = ans;

  if (is_automated) {
    std::string chosen_card = choose_card();
    std::string play_message =
        "TRICK" + std::to_string(lew_number) + chosen_card + "\r\n";
    output.first = play_message;
    last_card = chosen_card;
  }
  // step = TAKEN;
  return output;
}

std::string Client::handle_taken(const std::string& message) {
  if (debug) {
    std::cerr << "handle_taken\n";
  }
  if (!is_valid_taken_message(message) /*|| step != TAKEN*/) {
    return "";
  }
  int i;
  std::string lew;
  if (is_it_a_card(message.substr(6, 2)) ||
      is_it_a_card(message.substr(6, 3))) {
    i = 6;
    lew = message.substr(5, 1);
  } else {
    i = 7;
    lew = message.substr(5, 2);
  }
  int lew_nr = std::stoi(lew);
  if (lew_nr != lew_number) return "";
  std::vector<std::string> cards;
  while (i < (int)message.size()) {
    if (message[i] == '1') {
      cards.push_back(message.substr(i, 3));
      i += 3;
    } else {
      cards.push_back(message.substr(i, 2));
      i += 2;
    }
  }

  if ((int)hand.size() > 13 - lew_number) {
    int pos_id = 0;
    int next_id = 0;

    for (int i = 0; i < 4; i++) {
      if (players[i] == position) pos_id = i;
      if (players[i] == std::to_string(next_player)) next_id = i;
    }

    int pos = (4 + pos_id - next_id) % 4;

    std::string card = cards[pos];
    remove_card(card);
  }

  tricks_taken.push_back(cards);
  next_player = message[(int)message.size() - 1];
  std::string output;

  output = "A trick " + std::to_string(lew_number) + " is taken by " +
           next_player + ", cards ";

  for (int j = 0; j < 4; j++) {
    output = output + cards[j];
    if (j < 3) {
      output = output + ", ";
    } else {
      output = output + '.';
    }
  }
  lew_number++;
  // step = SCORE;
  return output;
}

std::string Client::handle_busy(const std::string& message) {
  if (debug) {
    std::cerr << "handle_busy\n";
  }
  std::string output = "Place busy, list of busy places received: ";
  for (int i = 4; i < (int)message.size(); i++) {
    output = output + message[i];
    if (i < (int)message.size() - 1) {
      output = output + ", ";
    } else {
      output = output + '.';
    }
  }
  output = output + "\n";
  return output;
}

std::string Client::handle_wrong(const std::string& message) {
  if (debug) {
    std::cerr << "handle_wrong\n";
  }

  hand.push_back(last_card);

  return "Wrong message received in trick " +
         message.substr(5, message.size() - 5) + ".\n";
}

std::string Client::handle_score(const std::string& message) {
  if (debug) {
    std::cerr << "handle_score\n";
  }
  if (!is_valid_score_message(
          message.substr(5, message.size() - 5)) /*|| step != SCORE*/) {
    return "";
  }
  return "The scores are:\n" +
         update_scores(message.substr(5, message.size() - 5));
}

std::string Client::handle_total(const std::string& message) {
  if (debug) {
    std::cerr << "handle_total\n";
  }
  if (!is_valid_score_message(
          message.substr(5, message.size() - 5)) /*|| step != SCORE*/) {
    return "";
  }
  lew_number = 1;
  return "The total scores are:\n" +
         update_scores(message.substr(5, message.size() - 5));
}

std::string Client::update_scores(const std::string& score) {
  int i = 0;
  int s = 0;
  int k;
  int player = 0;
  std::string output = "";
  while (i < (int)score.size()) {
    if (score[i] == 'N') {
      player = 0;
    } else if (score[i] == 'E') {
      player = 1;
    } else if (score[i] == 'S') {
      player = 2;
    } else if (score[i] == 'W') {
      player = 3;
    } else {
      k = (int)(score[i] - '0');
      if (s == 0) {
        s = k;
      } else {
        s = 10 * s + k;
      }
      if (i == (int)score.size() - 1 || score[i + 1] > '9' ||
          '0' > score[i + 1]) {
        output = output + players[player] + " | " + std::to_string(s) + "\n";
        s = 0;
      }
    }
    i++;
  }
  return output;
}

bool Client::is_valid_deal_message(const std::string& message) {
  std::regex deal_pattern(
      R"(DEAL[1-7][NESW]((2|3|4|5|6|7|8|9|10|J|Q|K|A)(C|D|H|S)){13})");
  return std::regex_match(message, deal_pattern);
}

bool Client::is_valid_trick_message(const std::string& message) {
  std::regex trick_pattern(
      R"(TRICK([1-9]|1[0-3])(((2|3|4|5|6|7|8|9|10|J|Q|K|A)(C|D|H|S)){0,3}))");
  return std::regex_match(message, trick_pattern);
}

bool Client::is_valid_taken_message(const std::string& message) {
  std::regex taken_pattern(
      R"(TAKEN([1-9]|1[0-3])(((2|3|4|5|6|7|8|9|10|J|Q|K|A)(C|D|H|S)){4})[NESW])");
  return std::regex_match(message, taken_pattern);
}

bool Client::is_valid_score_message(const std::string& message) {
  // Definicja wyrażenia regularnego
  std::regex total_pattern(R"([NESW]\d+[NESW]\d+[NESW]\d+[NESW]\d+)");

  // Sprawdzenie dopasowania wiadomości do wzoru
  return std::regex_match(message, total_pattern);
}

std::string Client::choose_card() {
  std::string card = "";
  if (cards_on_table.size() > 0) {
    std::string color_card = cards_on_table[0];
    char color = color_card[color_card.size() - 1];
    for (int i = hand.size() - 1; i >= 0; i--) {
      if (hand[i][hand[i].size() - 1] == color) {
        card = hand[i];
      }
    }

    if (card.size() == 0) card = hand.front();
  } else {
    card = hand.front();
  }
  auto it = std::find(hand.begin(), hand.end(), card);
  hand.erase(it);
  return card;
}

bool Client::remove_card(std::string card) {
  auto it = std::find(hand.begin(), hand.end(), card);
  if (it == hand.end()) return false;
  hand.erase(it);
  return true;
}

void Client::get_ips() {
  struct sockaddr_in server_addr, client_addr;
  socklen_t server_len = sizeof(server_addr);
  socklen_t client_len = sizeof(client_addr);
  int sockfd = client_socket;

  // Pobierz adres IP i port serwera
  if (getsockname(sockfd, (struct sockaddr*)&client_addr, &client_len) == -1) {
    perror("getsockname");
    exit(EXIT_FAILURE);
  }

  // Pobierz adres IP i port klienta
  if (getpeername(sockfd, (struct sockaddr*)&server_addr, &server_len) == -1) {
    perror("getpeername");
    exit(EXIT_FAILURE);
  }

  // Konwertuj adresy IP na czytelny dla człowieka format
  inet_ntop(AF_INET, &(server_addr.sin_addr), server_ip, INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

  // Uzyskanie portów
  server_port = ntohs(server_addr.sin_port);
  client_port = ntohs(client_addr.sin_port);
}

void Client::recieved(std::string message) {
  std::string report = "[";
  report = report + server_ip + ":" + std::to_string(server_port);
  report = report + ",";
  report = report + client_ip + ":" + std::to_string(client_port);
  report = report + ",";
  report = report + getTime() + "] ";
  report = report + message;
  poll_handlers[2].wrote(report);
}

void Client::sent(std::string message) {
  std::string report = "[";
  report = report + client_ip + ":" + std::to_string(client_port);
  report = report + ",";
  report = report + server_ip + ":" + std::to_string(server_port);
  report = report + ",";
  report = report + getTime() + "] ";
  report = report + message;
  poll_handlers[2].wrote(report);
}
