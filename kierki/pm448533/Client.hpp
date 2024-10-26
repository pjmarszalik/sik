#ifndef CLIENT_HPP
#define CLIENT_HPP
#include <fcntl.h>

#include <string>
#include <utility>
#include <vector>

#include "common.h"
class Client {
 public:
  Client(const char* host, uint16_t port, const std::string& position,
         bool is_automated, int ip_version);
  bool run();

 private:
  bool connect_to_server();
  bool play_game();
  std::string handle_deal(const std::string& message);
  std::pair<std::string, std::string> handle_trick(const std::string& message);
  std::string handle_taken(const std::string& message);
  std::string handle_busy(const std::string& message);
  std::string handle_wrong(const std::string& message);
  std::string handle_score(const std::string& message);
  std::string handle_total(const std::string& message);
  std::string update_scores(const std::string& score);
  std::string choose_card();
  void read_card(std::string buffer, int i);
  void read_cards(std::string buffer, int i);
  void read_tricks(std::string buffer, int i);
  bool remove_card(std::string card);
  bool is_valid_deal_message(const std::string& message);
  bool is_valid_trick_message(const std::string& message);
  bool is_valid_taken_message(const std::string& message);
  bool is_valid_score_message(const std::string& message);
  void sent(std::string message);
  void recieved(std::string message);
  void get_ips();

  std::string ip;
  char server_ip[INET_ADDRSTRLEN];
  char client_ip[INET_ADDRSTRLEN];
  int server_port;
  int client_port;

  bool debug = false;
  int connections = 4;
  struct Poll_handler poll_handlers[4];
  const char* host;
  uint16_t port;
  std::string position;
  bool is_automated;
  int ip_version;
  int client_socket;
  char next_player;
  int lew_number = 1;
  int game_type;
  int scores[4];
  std::vector<std::string> cards_on_table;
  std::vector<std::string> hand;
  std::vector<std::vector<std::string>> tricks_taken;
  std::string players[4] = {"N", "E", "S", "W"};
  std::string last_card;
  steps step = UNBORN;
};

#endif  // CLIENT_HPP