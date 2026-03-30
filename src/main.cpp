#include "network/udp_socket.hpp"
#include "server/publisher.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <memory>
#include <print>

std::unique_ptr<publisher> global_pub;

void handle_sigint(int) {
  if (global_pub) {
    std::println("\nInterrupt received. Stopping publisher...");
    global_pub->stop();
  }
}

auto main() -> int {
  std::signal(SIGINT, handle_sigint);

  auto sender_socket_expected = udp_socket<SocketType::Sender>::try_create();
  if (not sender_socket_expected.has_value()) {
    std::println(stderr, "Failed to create socket: {}",
                 sender_socket_expected.error().message());
    return 1;
  }

  // Multicast destination configuration
  const char *multicast_ip = "239.255.0.1";
  const uint16_t port = 12345;

  sockaddr_in dest_addr{};
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, multicast_ip, &dest_addr.sin_addr) <= 0) {
    std::println(stderr, "Invalid address/Address not supported");
    return 1;
  }

  // Configure publisher settings
  const uint64_t msg_per_sec = 100'000;
  const uint64_t burst_size = 1;

  global_pub = std::make_unique<publisher>(
      msg_per_sec, burst_size, std::move(sender_socket_expected.value()),
      dest_addr);

  std::println("Starting publisher streaming to {}:{} at {} msgs/sec",
               multicast_ip, port, msg_per_sec);

  // This will block until SIGINT (Ctrl+C) is caught
  global_pub->start();

  std::println("Publisher shut down gracefully.");
  return 0;
}
