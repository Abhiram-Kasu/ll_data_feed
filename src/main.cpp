#include "client/consumer.hpp"
#include "client/lf_queue.hpp"
#include "client/reader.hpp"
#include "common/types.hpp"
#include "network/udp_socket.hpp"
#include "server/publisher.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <print>
#include <ranges>
#include <ratio>
#include <span>
#include <string>
#include <thread>
#include <vector>
const char *multicast_ip = "239.255.0.1";
constexpr uint16_t port = 12345;
constexpr int num_listeners = 1;
constexpr size_t LF_QUEUE_SIZE = 100;
std::unique_ptr<publisher> global_pub;
// Use a vector to hold multiple readers
std::vector<std::unique_ptr<reader<ReaderState::Reading>>> readers;

std::vector<std::unique_ptr<consumer<std::chrono::nanoseconds>>> consumers;

void handle_sigint(int) {
  if (global_pub) {
    std::println("\nInterrupt received. Stopping publisher...");
    global_pub->stop();
  }

  for (auto &consumer : consumers) {
    consumer->stop_consuming();
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

auto main(int argc, char **argv) -> int {
  std::signal(SIGINT, handle_sigint);

  if (argc != 3) {
    std::println(
        stderr,
        "Need to specify messages per second and burst size: ./ll_data_feed "
        "-m<Messages Per Second> -b<Burst Size>");
    return EXIT_FAILURE;
  }
  auto args = std::span{argv, static_cast<size_t>(argc)}.subspan(1);

  uint64_t message_count = 0, burst_size = 0;

  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view arg{args[i]};

    if (arg.starts_with("-m")) {
      std::string_view val;
      if (arg.size() > 2) {
        val = arg.substr(2); // Attached: -m100
      } else if (i + 1 < args.size()) {
        val = args[++i]; // Separate: -m 100
      }

      auto [ptr, ec] =
          std::from_chars(val.data(), val.data() + val.size(), message_count);
      if (ec != std::errc{}) {
        std::println(stderr, "Error: Invalid message count '{}'", val);
        return EXIT_FAILURE;
      }
    } else if (arg.starts_with("-b")) {
      std::string_view val;
      if (arg.size() > 2) {
        val = arg.substr(2); // Attached: -b50
      } else if (i + 1 < args.size()) {
        val = args[++i]; // Separate: -b 50
      }

      auto [ptr, ec] =
          std::from_chars(val.data(), val.data() + val.size(), burst_size);
      if (ec != std::errc{}) {
        std::println(stderr, "Error: Invalid burst size '{}'", val);
        return EXIT_FAILURE;
      }
    }
  }

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

    auto queue = lf_queue<MarketUpdate, Permissions::Shared>(LF_QUEUE_SIZE);

    auto writer_queue = queue.to_writer();
    auto r = reader<ReaderState::Initialized>(
        writer_queue, std::move(receiver_socket_expected.value()));

    auto reading_state = r.start_reading(port);

    if (not reading_state.has_value()) {
      std::println(stderr, "Failed to start reading for listener {}: {}", i,
                   reading_state.error().message());
      return 1;
    }

    readers.push_back(std::make_unique<reader<ReaderState::Reading>>(
        std::move(reading_state.value())));

    auto consumer_ = std::make_unique<consumer<std::chrono::nanoseconds>>(
        queue.to_reader(), std::chrono::nanoseconds(1));

    consumer_->start_consuming();

    consumers.push_back(std::move(consumer_));
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

  global_pub = std::make_unique<publisher>(
      message_count, burst_size, std::move(sender_socket_expected.value()),
      dest_addr);

  std::println("Starting publisher streaming to {}:{} at {} msgs/sec",
               multicast_ip, port, message_count);

  global_pub->start();

  std::println("Application shut down gracefully.");
  return 0;
}
