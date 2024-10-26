#include "Client.hpp"

int main(int argc, char* argv[]) {
  const char* host = "";
  uint16_t port = 0;
  std::string position;
  bool is_automated = false;
  int ip_version = AF_UNSPEC;  // Default to unspecified

  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "-h" && i + 1 < argc) {
      host = argv[++i];
    } else if (std::string(argv[i]) == "-p" && i + 1 < argc) {
      port = read_port(argv[++i]);
    } else if (std::string(argv[i]) == "-N" || std::string(argv[i]) == "-E" ||
               std::string(argv[i]) == "-S" || std::string(argv[i]) == "-W") {
      position = argv[i][1];
    } else if (std::string(argv[i]) == "-a") {
      is_automated = true;
    } else if (std::string(argv[i]) == "-4") {
      ip_version = AF_INET;
    } else if (std::string(argv[i]) == "-6") {
      ip_version = AF_INET6;
    }
  }

  if (std::string(host).empty() || port == 0 || position.empty()) {
    std::cerr << "Error: host, port, and position are required." << std::endl;
    return 1;
  }

  Client client(host, port, position, is_automated, ip_version);

  if (client.run()) {
    return 0;
  }
  return 1;
}