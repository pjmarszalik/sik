#include "Server.hpp"

int main(int argc, char* argv[]) {
  int port = 0;
  std::string filename;
  int timeout = 5;

  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "-p" && i + 1 < argc) {
      port = read_port(argv[++i]);
    } else if (std::string(argv[i]) == "-f" && i + 1 < argc) {
      filename = argv[++i];
    } else if (std::string(argv[i]) == "-t" && i + 1 < argc) {
      timeout = std::stoi(argv[++i]);
    }
  }

  if (filename.empty()) {
    std::cerr << "Error: game definition file is required." << std::endl;
    return 1;
  }

  Server server(port, filename, timeout);

  if (server.run()) {
    return 0;
  }
  return 1;
}
