#include "network/udp_socket.hpp"
#include <print>

auto main() -> int {

  auto sender = udp_socket<SocketType::Sender>::try_create();
}
