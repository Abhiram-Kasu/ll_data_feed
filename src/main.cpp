#include "client/reader.hpp"
#include "network/udp_socket.hpp"
#include "server/publisher.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <print>
#include <ranges>
#include <thread>
#include <vector>

std::unique_ptr<publisher> global_pub;
// Use a vector to hold multiple readers
std::vector<std::unique_ptr<reader<ReaderState::Reading>>> readers;

void handle_sigint(int) {
  if (global_pub) {
    std::println("\nInterrupt received. Stopping publisher...");
    global_pub->stop();
  }

  if (!readers.empty()) {
    std::println("\nTrying to stop readers...");
    std::vector<diag> total_packet_dropped;
    total_packet_dropped.reserve(readers.size());

    for (auto &r : readers) {
      if (r) {
        r->stop_reading();
        total_packet_dropped.push_back(r->get_diag());
        r->join();
      }
    }

    for (auto i{0uz}; i < total_packet_dropped.size(); i++) {
      std::println("receiver: {:02} had {:L} packets dropped and {:L} "
                   "packets received",
                   i, total_packet_dropped[i].num_dropped,
                   total_packet_dropped[i].num_received);
    }
  }
}

auto main() -> int {
  std::signal(SIGINT, handle_sigint);

  const char *multicast_ip = "239.255.0.1";
  const uint16_t port = 12345;
  const int num_listeners = 10;

  for (int i = 0; i < num_listeners; ++i) {
    auto receiver_socket_expected =
        udp_socket<SocketType::Receiever>::try_create();

    if (not receiver_socket_expected.has_value()) {
      std::println(stderr, "Failed to create receiver socket {}: {}", i,
                   receiver_socket_expected.error().message());
      return 1;
    }

    if (auto res = receiver_socket_expected->set_socket_reuse(); not res) {
      std::println(stderr, "failed to set socket_reuse for socket {}: {}", i,
                   res.error().message());
      return 1;
    };

    auto r = reader<ReaderState::Initialized>(
        nullptr, std::move(receiver_socket_expected.value()));

    auto reading_state = r.start_reading(port);

    if (not reading_state.has_value()) {
      std::println(stderr, "Failed to start reading for listener {}: {}", i,
                   reading_state.error().message());
      return 1;
    }

    readers.push_back(std::make_unique<reader<ReaderState::Reading>>(
        std::move(reading_state.value())));
  }

  std::println("{} Readers started on port {}.", num_listeners, port);

  auto sender_socket_expected = udp_socket<SocketType::Sender>::try_create();
  if (not sender_socket_expected.has_value()) {
    std::println(stderr, "Failed to create sender socket: {}",
                 sender_socket_expected.error().message());
    return 1;
  }

  auto &sender_socket = sender_socket_expected.value();

  sockaddr_in dest_addr{};
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, multicast_ip, &dest_addr.sin_addr) <= 0) {
    std::println(stderr, "Invalid address/Address not supported");
    return 1;
  }

  // Configure publisher settings (low rate to easily view reader output)
  const uint64_t msg_per_sec = 1000;
  const uint64_t burst_size = 500;

  global_pub = std::make_unique<publisher>(
      msg_per_sec, burst_size, std::move(sender_socket_expected.value()),
      dest_addr);

  std::println("Starting publisher streaming to {}:{} at {} msgs/sec",
               multicast_ip, port, msg_per_sec);

  global_pub->start();

  std::println("Application shut down gracefully.");
  return 0;
}
