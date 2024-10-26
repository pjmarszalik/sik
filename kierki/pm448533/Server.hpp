#ifndef SERVER_HPP
#define SERVER_HPP
#include <fcntl.h>
#include <netinet/in.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common.h"

struct Deal {
  int type;
  char startingPlayer;
  std::unordered_map<std::string, std::vector<std::string>> cards;
};

class Server {
 public:
  Server(int port, const std::string& filename, int timeout);
  bool run();

 private:
  std::vector<std::string> parseCards(const std::string& cardsStr);
  bool load_game_definition();
  void start_game();
  void calculate_scores();
  bool create_socket();
  std::string determine_winner();
  int parse_iam_message(std::string message, int i);
  int parse_trick_message(std::string message, int i);
  bool has_a_card_int(std::string card, int i, char color);
  bool has_a_card_position(std::string card, std::string position, char color);
  void prepare_wrong(int i);
  void prepare_busy(int i);
  void prepare_taken(int i);
  void prepare_score(int i);
  void prepare_total(int i);
  void prepare_deal(int i);
  void prepare_trick(int i);
  void next_player();
  void inform(int i);
  void inform_deal(int i);
  void inform_taken(int i, int k);
  void recieved(std::string message, int i);
  void sent(std::string message, int i);
  void wrote(std::string message, int i);
  void after_scoretotal(int i);

  std::string ip;
  bool debug = false;
  uint16_t port;
  std::string filename;
  int timeout;
  int server_socket;
  bool dead = false;
  std::unordered_map<std::string, int> player_sockets;
  std::unordered_map<int, std::string> socket_players;
  std::unordered_map<std::string, int> scores;
  std::unordered_map<std::string, std::vector<std::string>> player_hands;
  std::unordered_map<std::string, int> player_total;
  std::unordered_map<std::string, int> player_scores;
  std::unordered_map<std::string, steps> player_steps;
  std::vector<std::vector<std::string>> tricks;
  std::vector<std::string> trick;
  std::vector<std::string> winners;
  std::vector<Deal> deals;
  int current_deal_index = 1;
  int lew_number = 1;
  std::string current_player;
  int current_player_id;
  std::string starting_player;
  struct Poll_handler poll_handlers[CONNECTIONS];
  int places = 0;
  int active_clients = 0;
  std::string players[4] = {"N", "E", "S", "W"};

  enum states { GATHERING, DEALING, TRICKING, ENDING };
  states state = GATHERING;
};

#endif  // SERVER_HPP