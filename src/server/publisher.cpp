#include "publisher.hpp"
#include "common/types.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <print>

auto publisher::start() -> void {

  // (1 second in nanoseconds * m_burst_size) / rate
  static const uint64_t burst_interval_ns =
      (1'000'000'000ULL * m_burst_size) / m_num_msg_per_second;

  auto next_burst_time = std::chrono::steady_clock::now();
  uint64_t seq_count = 0;
  uint64_t total_sent = 0;
  uint64_t total_failed = 0;

  std::println("Starting publisher: {} msgs/sec in bursts of {}",
               m_num_msg_per_second, m_burst_size);

  while (not m_stop_token.load(std::memory_order_acquire)) {
    auto now = std::chrono::steady_clock::now();

    // Check if its burst time
    if (now >= next_burst_time) {

      for (uint32_t i = 0; i < m_burst_size; ++i) {
        auto data = MarketUpdate{
            .seq = ++seq_count,
            .send_timestamp_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()),
            .price = 100.50 + ((rand() % 100) / 100.0f),
            .size = 20};

        auto data_raw = std::span<const uint8_t>(
            reinterpret_cast<const uint8_t *>(&data), sizeof(data));

        if (auto res = m_sending_socket.write(data_raw, m_destination_address);
            res.has_value()) {
          total_sent++;
        } else {
          total_failed++;
          std::println(stderr, "Failed to send: {}", res.error().message());
        }
      }

      next_burst_time += std::chrono::nanoseconds(burst_interval_ns);

      // Allows producer to catch up
      if (next_burst_time < now) {
        next_burst_time = now;
        std::println("Slowing Down");
      }
    } else {
      // maybe use __builtin_arm_yield() later on
    }
  }

  std::println("Publisher stopped. Total sent: {}, Total failed: {}",
               total_sent, total_failed);
}

auto publisher::stop() -> void {
  m_stop_token.store(true, std::memory_order_release);
}
