#include "Server.hpp"

Server::Server(int port, const std::string &filename, int timeout)
    : port(port), filename(filename), timeout(timeout), server_socket(-1) {}

bool Server::create_socket() {
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  fcntl(server_socket, F_SETFL, O_NONBLOCK);
  if (server_socket == -1) {
    if (debug) {
      std::cerr << "Error: could not create socket." << std::endl;
    }
    return false;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;  // IPv4
  server_addr.sin_addr.s_addr =
      htonl(INADDR_ANY);  // Listening on all interfaces.
  server_addr.sin_port = htons(port);

  // If port is 0, let the system assign a port
  if (port == 0) {
    server_addr.sin_port = 0;
  }

  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) == -1) {
    if (debug) {
      std::cerr << "Error: could not bind socket." << std::endl;
    }
    close(server_socket);
    return false;
  }

  // Retrieve the assigned port number
  if (port == 0) {
    socklen_t len = (socklen_t)sizeof server_addr;
    if (getsockname(server_socket, (struct sockaddr *)&server_addr, &len) < 0) {
      if (debug) {
        std::cerr << "Error: could not get assigned port." << std::endl;
      }
      return false;
    } else {
      if (debug) {
        std::cerr << "Server is listening on port: "
                  << ntohs(server_addr.sin_port) << std::endl;
      }
    }
  }

  if (listen(server_socket, QUEUE_LENGTH) == -1) {
    if (debug) {
      std::cerr << "Error: could not listen on socket." << std::endl;
    }
    close(server_socket);
    return false;
  }

  // Find out what port the server is actually listening on.
  socklen_t lenght = (socklen_t)sizeof server_addr;
  if (getsockname(server_socket, (struct sockaddr *)&server_addr, &lenght) <
      0) {
    if (debug) {
      std::cerr << "Error: Can't get server address." << std::endl;
    }
    return false;
  }
  return true;
}

bool Server::run() {
  if (!load_game_definition()) return false;
  if (!create_socket()) return false;

  struct pollfd poll_descriptors[CONNECTIONS];

  // The main socket has index 0.
  poll_descriptors[0].fd = server_socket;
  poll_descriptors[0].events = POLLIN;

  for (int i = 1; i < CONNECTIONS; ++i) {
    poll_descriptors[i].fd = -1;
    poll_descriptors[i].events = POLLIN;
    poll_handlers[i] = Poll_handler(&poll_descriptors[i]);
  }

  struct sockaddr_in client_address;
  bool finish = false;

  player_sockets["N"] = -1;
  player_sockets["S"] = -1;
  player_sockets["W"] = -1;
  player_sockets["E"] = -1;

  player_steps["N"] = UNBORN;
  player_steps["S"] = UNBORN;
  player_steps["W"] = UNBORN;
  player_steps["E"] = UNBORN;

  player_scores["N"] = 0;
  player_scores["S"] = 0;
  player_scores["W"] = 0;
  player_scores["E"] = 0;

  player_total["N"] = 0;
  player_total["S"] = 0;
  player_total["W"] = 0;
  player_total["E"] = 0;

  do {
    for (int i = 0; i < CONNECTIONS; ++i) {
      poll_descriptors[i].revents = 0;
    }

    // After Ctrl-C the main socket is closed.
    if (finish && poll_descriptors[0].fd >= 0) {
      close(poll_descriptors[0].fd);
      poll_descriptors[0].fd = -1;
    }

    int poll_status = poll(poll_descriptors, CONNECTIONS, TIMEOUT);
    if (poll_status == -1) {
      if (errno == EINTR) {
        if (debug) {
          std::cerr << "Error: Interrupted system call." << std::endl;
        }
      } else {
        if (debug) {
          std::cerr << "Error: Problem with executing poll." << std::endl;
        }
      }
    } else if (poll_status > 0) {
      if (!finish && (poll_descriptors[0].revents & POLLIN)) {
        // New connection: new client is accepted.
        socklen_t size = sizeof client_address;
        int client_fd = accept(poll_descriptors[0].fd,
                               (struct sockaddr *)&client_address, &size);
        fcntl(client_fd, F_SETFL, O_NONBLOCK);

        if (client_fd < 0) {
          if (debug) {
            std::cerr << "Error: Problem with executing accept." << std::endl;
          }
        }
        // Searching for a free slot.
        int slot = -1;
        bool accepted = false;
        for (int i = 1; i < CONNECTIONS; ++i) {
          if (poll_descriptors[i].fd == -1) {
            if (debug) {
              std::cerr << "received new connection (" << i << ")\n";
            }
            poll_descriptors[i].fd = client_fd;
            poll_descriptors[i].events = POLLIN;
            active_clients++;
            accepted = true;
            poll_handlers[i] = Poll_handler(&poll_descriptors[i]);
            poll_handlers[i].step = IAM;
            poll_handlers[i].start_time();
            slot = i;
            break;
          }
        }
        if (!accepted) {
          close(client_fd);
          if (debug) {
            std::cerr << "too many clients\n";
          }
        } else {
          char const *client_ip = inet_ntoa(client_address.sin_addr);
          poll_handlers[slot].ip = client_ip;
          uint16_t client_port = ntohs(client_address.sin_port);
          if (debug) {
            std::cerr << "accepted connection from " << client_ip << ": "
                      << client_port << "\n";
          }
        }
      }

    } else {
      // std::cout << TIMEOUT << " milliseconds passed without any events\n";
    }

    // Serve data connections.
    for (int i = 1; i < CONNECTIONS; ++i) {
      if (poll_handlers[i].pfd->fd == -1) {
        continue;
      }

      if (poll_handlers[i].step == GIVE &&
          poll_handlers[i].is_too_late(timeout)) {
        prepare_trick(i);
      }

      if ((poll_handlers[i].step == DEAD &&
           poll_handlers[i].write_length == 0)) {
        poll_handlers[i].close_handler();
        active_clients--;
        places--;
        player_sockets[socket_players[i]] = -1;
      } else if (poll_handlers[i].step == IAM &&
                 poll_handlers[i].is_too_late(timeout)) {
        poll_handlers[i].close_handler();
        active_clients--;
      } else if (poll_handlers[i].step != UNBORN) {
        poll_handlers[i].act(&active_clients, &places);
        if (poll_handlers[i].pfd->fd == -1) {
          player_sockets[socket_players[i]] = -1;
        }
      }
      poll_handlers[i].clean();
    }

    for (int i = 1; i < CONNECTIONS; i++) {
      if (poll_handlers[i].pfd->fd == -1) {
        continue;
      }

      if (poll_handlers[i].step == UNBORN || poll_handlers[i].step == DEAD)
        continue;

      if (poll_handlers[i].pfd->revents & (POLLIN | POLLERR)) {
        std::string buffer = convertToString(poll_handlers[i].read_buffer,
                                             poll_handlers[i].read_pointer);
        size_t pos = buffer.find("\r\n");
        if (pos != std::string::npos) {
          std::string message = buffer.substr(0, pos);
          recieved(buffer.substr(0, pos + 2), i);
          poll_handlers[i].shift_read_buffer(pos + 2);

          int ret = 0;
          if (poll_handlers[i].step == IAM) {
            ret = parse_iam_message(message, i);
          } else if (poll_handlers[i].step == GIVE) {
            ret = parse_trick_message(message, i);
          } else {
            ret = 2;
          }
          if (ret == 1) {  // closing connection
            if (debug) {
              std::cerr << "Error: Expected IAM, got something else. Ending "
                           "connection."
                        << std::endl;
            }
            poll_handlers[i].close_handler();
            places--;
            active_clients--;
            player_sockets[socket_players[i]] = -1;
          } else if (ret == 2) {  // Prepare to send WRONG
            if (debug) {
              std::cerr << "sending wrong about message: " << message
                        << std::endl;
            }
            prepare_wrong(i);
          } else if (ret == 3) {  // Prepare to send BUSY
            prepare_busy(i);
          }
        }
      }
      if (poll_handlers[i].pfd->revents & POLLOUT) {
        if (poll_handlers[i].write_pointer == poll_handlers[i].write_length &&
            poll_handlers[i].step == DEAD) {
          if (debug) {
            std::cerr << "Error: Descriptor is DEAD and has nothing to say. "
                         "Ending connection."
                      << std::endl;
          }
          poll_handlers[i].close_handler();
          places--;
          active_clients--;
          player_sockets[socket_players[i]] = -1;
        }
      }
    }

    for (int i = 1; i < CONNECTIONS; i++) {
      if (poll_handlers[i].step == UNBORN || poll_handlers[i].step == DEAD)
        continue;
      if (places == 4 && current_player == socket_players[i]) {  // make a move
        if (poll_handlers[i].step == DEAL)
          prepare_deal(i);
        else if (poll_handlers[i].step == TRICK)
          prepare_trick(i);
        else if (poll_handlers[i].step == TAKEN)
          prepare_taken(i);
        else if (poll_handlers[i].step == SCORE) {
          prepare_score(i);
          prepare_total(i);
          after_scoretotal(i);
        }
      }
    }

    if (active_clients == 0 && dead) finish = true;

  } while (!finish || active_clients > 0);

  if (poll_descriptors[0].fd >= 0) {
    close(poll_descriptors[0].fd);
  }

  close(server_socket);
  return true;
}

// Returning  0 if ok
// 1 if error so bad you have to close connection
// 2 if you have to send back WRONG
// 3 if you have to answer BUSY
int Server::parse_iam_message(std::string message, int i) {
  if (debug) {
    std::cerr << "parse_iam_message\n";
  }
  size_t pos = message.find("IAM");
  if (pos != 0) return 1;
  std::string position = message.substr(3, 1);
  if (position != "N" && position != "S" && position != "W" &&
      position != "E") {
    return 1;
  }
  if (player_sockets[position] != -1) {
    return 3;
  }
  poll_handlers[i].step = DEAL;
  player_sockets[position] = i;
  socket_players[i] = position;
  places++;
  if (tricks.size() > 0) {
    inform(i);
  } else {
    player_steps[position] = DEAL;
  }
  return 0;
}

// Returning  0 if ok
// 1 if error so bad you have to close connection
// 2 if you have to send back WRONG
// 3 if you have to answer BUSY
int Server::parse_trick_message(std::string message, int i) {
  if (debug) {
    std::cerr << "parse_trick_message\n";
  }
  size_t pos = message.find("TRICK");
  if (pos != 0) {
    return 2;
  }
  if (poll_handlers[i].step != GIVE) return 2;
  std::string card = message.substr(message.size() - 2, 2);
  std::regex cardPattern(R"((2|3|4|5|6|7|8|9|10|J|Q|K|A)(C|D|H|S))");
  if (!std::regex_match(card, cardPattern)) {
    card = message.substr(message.size() - 3, 3);
  }
  if (!std::regex_match(card, cardPattern)) {
    return 2;
  }
  char color = 'H';
  if (trick.size() > 0) {
    color = trick[0][trick[0].size() - 1];
  }
  if (!has_a_card_int(card, i, color)) {
    return 2;
  }
  message = message.substr(5, message.size() - 5 - card.size());
  for (int j = 0; j < (int)message.size(); j++) {
    if (message[j] < '0' || '9' < message[j]) return 2;
  }
  int lew_nr = std::stoi(message);
  if (lew_nr != lew_number) return 2;
  trick.push_back(card);
  auto it = std::find(player_hands[socket_players[i]].begin(),
                      player_hands[socket_players[i]].end(), card);
  player_hands[socket_players[i]].erase(it);

  poll_handlers[i].step = TAKEN;
  player_steps[socket_players[i]] = TAKEN;
  next_player();
  return 0;
}

bool Server::has_a_card_int(std::string card, int i, char color) {
  std::string position = socket_players[i];
  if (current_player != position) return false;
  return has_a_card_position(card, position, color);
}

bool Server::has_a_card_position(std::string card, std::string position,
                                 char color) {
  std::vector<std::string> cards = player_hands[position];
  if (trick.size() > 0 && color != card[card.size() - 1]) {
    for (auto s : cards) {
      if (s[s.size() - 1] == color) return false;
    }
  }
  for (auto s : cards) {
    if (card == s) {
      return true;
    }
  }
  return false;
}

void Server::prepare_wrong(int i) {
  if (debug) {
    std::cerr << "prepare_wrong\n";
  }
  std::string message = "WRONG" + std::to_string(lew_number) + "\r\n";
  wrote(message, i);
}
void Server::prepare_busy(int i) {
  if (debug) {
    std::cerr << "prepare_busy\n";
  }
  std::string message = "BUSY";

  for (int j = 0; j < 4; j++) {
    if (player_sockets[players[j]] != -1) message = message + players[j];
  }
  message = message + "\r\n";
  wrote(message, i);
  player_steps[socket_players[i]] = DEAD;
  poll_handlers[i].step = DEAD;
}

void Server::prepare_deal(int i) {
  if (debug) {
    std::cerr << "prepare_deal\n";
  }
  std::string player = socket_players[i];
  Deal deal = deals[current_deal_index - 1];
  std::string message =
      "DEAL" + std::to_string(deal.type) + deal.startingPlayer;
  std::vector<std::string> cards = deal.cards[player];
  for (int j = 0; j < (int)cards.size(); j++) {
    message = message + cards[j];
    player_hands[player].push_back(cards[j]);
  }
  message = message + "\r\n";
  wrote(message, i);
  poll_handlers[i].step = TRICK;
  player_steps[socket_players[i]] = TRICK;
  tricks.clear();
  next_player();
}

void Server::prepare_trick(int i) {
  if (debug) {
    std::cerr << "prepare_trick\n";
  }
  std::string message = "TRICK" + std::to_string(lew_number);
  for (int j = 0; j < (int)trick.size(); j++) message = message + trick[j];
  message = message + "\r\n";
  wrote(message, i);
  player_steps[socket_players[i]] = GIVE;
  poll_handlers[i].step = GIVE;
  poll_handlers[i].start_time();
}

void Server::prepare_taken(int i) {
  if (debug) {
    std::cerr << "prepare_taken\n";
  }
  std::string message = "TAKEN" + std::to_string(lew_number);
  for (int j = 0; j < (int)trick.size(); j++) {
    message = message + trick[j];
  }

  std::string win = determine_winner();
  if (socket_players[i] == starting_player) {
    winners.push_back(win);
  }
  message = message + win + "\r\n";

  if (lew_number < 13) {
    poll_handlers[i].step = TRICK;
    player_steps[socket_players[i]] = TRICK;
  } else {
    poll_handlers[i].step = SCORE;
    player_steps[socket_players[i]] = SCORE;
  }

  if (players[(current_player_id + 1) % 4] == starting_player) {
    std::vector<std::string> trick_cpy = trick;
    tricks.push_back(trick_cpy);
    trick.clear();
    lew_number++;
    if (poll_handlers[i].step != TRICK) {
      lew_number = 1;
      current_deal_index++;
    }
    starting_player = win;
    current_player = win;
    for (int j = 0; j < 4; j++) {
      if (players[j] == current_player) current_player_id = j;
    }

  } else {
    next_player();
  }
  wrote(message, i);
}

void Server::prepare_score(int i) {
  if (debug) {
    std::cerr << "prepare_score\n";
  }
  std::string message = "SCORE";
  if (socket_players[i] == starting_player) {
    calculate_scores();
  }
  for (int j = 0; j < 4; j++) {
    message = message + players[j] + std::to_string(player_scores[players[j]]);
  }
  if (players[(current_player_id + 1) % 4] == starting_player) {
    for (int j = 0; j < 4; j++) {
      player_scores[players[j]] = 0;
    }
  }
  message = message + "\r\n";
  wrote(message, i);
  poll_handlers[i].step = TOTAL;
  player_steps[socket_players[i]] = TOTAL;
}
void Server::prepare_total(int i) {
  if (debug) {
    std::cerr << "prepare_total\n";
  }
  std::string message = "TOTAL";
  for (int j = 0; j < 4; j++) {
    message = message + players[j] + std::to_string(player_total[players[j]]);
  }
  message = message + "\r\n";
  wrote(message, i);
}

void Server::after_scoretotal(int i) {
  poll_handlers[i].step = DEAL;
  player_steps[socket_players[i]] = DEAL;
  next_player();
  if (current_player == starting_player &&
      current_deal_index <= (int)deals.size()) {
    starting_player = deals[current_deal_index - 1].startingPlayer;
    current_player = starting_player;
    for (int j = 0; j < 4; j++) {
      if (players[j] == current_player) current_player_id = j;
    }
  }
  if (current_deal_index > (int)deals.size()) {
    poll_handlers[i].step = DEAD;
    player_steps[socket_players[i]] = DEAD;
    dead = true;
  }
}

void Server::inform(int i) {
  if (debug) {
    std::cerr << "inform\n";
  }
  inform_deal(i);
  for (int j = 0; j < (int)tricks.size(); j++) {
    inform_taken(i, j);
  }
  steps step = player_steps[socket_players[i]];
  if (step == GIVE) {
    step = TRICK;
  }
  poll_handlers[i].step = step;
}

void Server::inform_deal(int i) {
  if (debug) {
    std::cerr << "inform_deal\n";
  }
  std::string player = socket_players[i];
  Deal deal = deals[current_deal_index - 1];
  std::string message =
      "DEAL" + std::to_string(deal.type) + deal.startingPlayer;
  std::vector<std::string> cards = deal.cards[player];
  for (int j = 0; j < (int)cards.size(); j++) {
    message = message + cards[j];
    player_hands[player].push_back(cards[j]);
  }
  message = message + "\r\n";
  wrote(message, i);
}

void Server::inform_taken(int i, int k) {
  if (debug) {
    std::cerr << "inform_taken\n";
  }
  std::string message = "TAKEN" + std::to_string(k + 1);
  for (int j = 0; j < (int)tricks[k].size(); j++) {
    message = message + tricks[k][j];
  }

  message = message + winners[k] + "\r\n";
  wrote(message, i);
}

std::vector<std::string> Server::parseCards(const std::string &cardsStr) {
  std::regex cardPattern(R"((2|3|4|5|6|7|8|9|10|J|Q|K|A)(C|D|H|S))");
  std::vector<std::string> cards;
  std::string card;
  size_t pos = 0;
  while (pos < cardsStr.size()) {
    size_t cardLength = (cardsStr[pos] == '1' && cardsStr[pos + 1] == '0')
                            ? 3
                            : 2;  // Uwzględnia karty 10
    card = cardsStr.substr(pos, cardLength);
    if (!std::regex_match(card, cardPattern)) {
      throw std::invalid_argument("Invalid card format: " + card);
    }
    cards.push_back(card);
    pos += cardLength;
  }
  if (cards.size() < 13) {
    throw std::invalid_argument("Missing cards");
  } else if (cards.size() > 13) {
    throw std::invalid_argument("Too many cards");
  }
  return cards;
}

bool Server::load_game_definition() {
  std::ifstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open file");
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
  }

  if (lines.size() % 5 != 0) {
    throw std::invalid_argument("Invalid game definition file format");
  }

  std::regex gameTypePattern(R"(^([1-7])([NESW])$)");
  for (size_t i = 0; i < lines.size(); i += 5) {
    std::smatch match;
    if (!std::regex_match(lines[i], match, gameTypePattern)) {
      throw std::invalid_argument(
          "Invalid game type or starting player format: " + lines[i]);
    }

    Deal deal;
    deal.type = std::stoi(match[1]);
    deal.startingPlayer = match[2].str()[0];

    deal.cards["N"] = parseCards(lines[i + 1]);
    deal.cards["E"] = parseCards(lines[i + 2]);
    deal.cards["S"] = parseCards(lines[i + 3]);
    deal.cards["W"] = parseCards(lines[i + 4]);

    deals.push_back(deal);
  }

  // Inicjalizacja starting_player i current_player
  starting_player = deals[0].startingPlayer;
  current_player = starting_player;
  for (int i = 0; i < 4; i++) {
    if (players[i] == current_player) current_player_id = i;
  }
  return true;
}

void Server::next_player() {
  current_player = players[(++current_player_id) % 4];
}

std::string Server::determine_winner() {
  char color, new_color;
  int winner = 0;
  char card = 0;
  char new_card;

  if (trick[0].size() == 2) {
    color = trick[0][1];
    card = trick[0][0];
  } else {
    color = trick[0][2];
    card = '9' + 1;
  }
  if (card == 'K')
    card = 'Y';
  else if (card == 'A')
    card = 'Z';

  for (int i = 1; i < 4; i++) {
    if (trick[i].size() == 2) {
      new_color = trick[i][1];
      new_card = trick[i][0];
    } else {
      new_color = trick[i][2];
      new_card = '9' + 1;
    }
    if (new_card == 'K') new_card = 'Y';
    if (new_card == 'A') new_card = 'Z';
    if (new_color == color && new_card > card) {
      card = new_card;
      winner = i;
    }
  }

  if (starting_player == "E")
    winner++;
  else if (starting_player == "S")
    winner += 2;
  else if (starting_player == "W")
    winner += 3;

  winner %= 4;
  return players[winner];
}

void Server::calculate_scores() {
  if (deals[current_deal_index - 2].type == 1 ||
      deals[current_deal_index - 2].type == 7) {
    for (int i = 0; i < (int)winners.size(); i++) {
      player_scores[winners[i]]++;
    }
  }
  std::string card;
  std::vector<std::string> cards;
  if (deals[current_deal_index - 2].type == 2 ||
      deals[current_deal_index - 2].type == 7) {
    char heart = 'H';
    for (int i = 0; i < (int)tricks.size(); i++) {
      cards = tricks[i];
      for (int j = 0; j < (int)cards.size(); j++) {
        if (cards[j][cards[j].size() - 1] == heart) {
          player_scores[winners[i]]++;
        }
      }
    }
  }
  if (deals[current_deal_index - 2].type == 3 ||
      deals[current_deal_index - 2].type == 7) {
    char queen = 'Q';
    for (int i = 0; i < (int)tricks.size(); i++) {
      cards = tricks[i];
      for (int j = 0; j < (int)cards.size(); j++) {
        if (cards[j][0] == queen) {
          player_scores[winners[i]] += 5;
        }
      }
    }
  }
  if (deals[current_deal_index - 2].type == 4 ||
      deals[current_deal_index - 2].type == 7) {
    char jack = 'J';
    char king = 'K';
    for (int i = 0; i < (int)tricks.size(); i++) {
      cards = tricks[i];
      for (int j = 0; j < (int)cards.size(); j++) {
        if (cards[j][0] == jack || cards[j][0] == king) {
          player_scores[winners[i]] += 2;
        }
      }
    }
  }
  if (deals[current_deal_index - 2].type == 5 ||
      deals[current_deal_index - 2].type == 7) {
    std::string king_of_hearts = "KH";
    for (int i = 0; i < (int)tricks.size(); i++) {
      cards = tricks[i];
      for (int j = 0; j < (int)cards.size(); j++) {
        if (cards[j] == king_of_hearts) {
          player_scores[winners[i]] += 18;
        }
      }
    }
  }
  if (deals[current_deal_index - 2].type == 6 ||
      deals[current_deal_index - 2].type == 7) {
    player_scores[winners[0]] += 10;
    player_scores[winners[12]] += 10;
  }

  for (auto s : players) {
    player_total[s] += player_scores[s];
  }

  winners.clear();
}

void Server::recieved(std::string message, int i) {
  struct sockaddr_in server_addr, client_addr;
  socklen_t server_len = sizeof(server_addr);
  socklen_t client_len = sizeof(client_addr);
  int sockfd = poll_handlers[i].pfd->fd;

  // Pobierz adres IP i port serwera
  if (getsockname(sockfd, (struct sockaddr *)&server_addr, &server_len) == -1) {
    perror("getsockname");
    exit(EXIT_FAILURE);
  }

  // Pobierz adres IP i port klienta
  if (getpeername(sockfd, (struct sockaddr *)&client_addr, &client_len) == -1) {
    perror("getpeername");
    exit(EXIT_FAILURE);
  }

  char server_ip[INET_ADDRSTRLEN];
  char client_ip[INET_ADDRSTRLEN];

  // Konwertuj adresy IP na czytelny dla człowieka format
  inet_ntop(AF_INET, &(server_addr.sin_addr), server_ip, INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

  // Uzyskanie portów
  int server_port = ntohs(server_addr.sin_port);
  int client_port = ntohs(client_addr.sin_port);

  std::string report = "[";
  report = report + client_ip + ":" + std::to_string(client_port);
  report = report + ",";
  report = report + server_ip + ":" + std::to_string(server_port);
  report = report + ",";
  report = report + getTime() + "] ";
  report = report + message;
  std::cout << report;
}

void Server::sent(std::string message, int i) {
  struct sockaddr_in server_addr, client_addr;
  socklen_t server_len = sizeof(server_addr);
  socklen_t client_len = sizeof(client_addr);
  int sockfd = poll_handlers[i].pfd->fd;

  // Pobierz adres IP i port serwera
  if (getsockname(sockfd, (struct sockaddr *)&server_addr, &server_len) == -1) {
    perror("getsockname");
    exit(EXIT_FAILURE);
  }

  // Pobierz adres IP i port klienta
  if (getpeername(sockfd, (struct sockaddr *)&client_addr, &client_len) == -1) {
    perror("getpeername");
    exit(EXIT_FAILURE);
  }

  char server_ip[INET_ADDRSTRLEN];
  char client_ip[INET_ADDRSTRLEN];

  // Konwertuj adresy IP na czytelny dla człowieka format
  inet_ntop(AF_INET, &(server_addr.sin_addr), server_ip, INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

  // Uzyskanie portów
  int server_port = ntohs(server_addr.sin_port);
  int client_port = ntohs(client_addr.sin_port);

  std::string report = "[";
  report = report + server_ip + ":" + std::to_string(server_port);
  report = report + ",";
  report = report + client_ip + ":" + std::to_string(client_port);
  report = report + ",";
  report = report + getTime() + "] ";
  report = report + message;
  std::cout << report;
}

void Server::wrote(std::string message, int i) {
  poll_handlers[i].wrote(message);
  sent(message, i);
}
