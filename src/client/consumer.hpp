#pragma once
#include "common/types.hpp"
#include "lf_queue.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <print>
#include <ratio>
#include <stop_token>
#include <thread>

template <typename T>
concept DurationLike =
    requires(T t) { std::chrono::duration_cast<std::chrono::nanoseconds>(t); };

template <DurationLike Duration> struct consumer {
  using lf_queue_read = lf_queue<MarketUpdate, Permissions::Read>;

public:
  consumer(lf_queue_read queue, Duration &&delay_per_update)
      : m_queue(queue),
        m_delay_per_update(std::forward<Duration>(delay_per_update)) {}

  auto start_consuming() -> void {

    m_consuming_thread = std::make_unique<std::jthread>(
        [token = m_stop_source.get_token(), queue = m_queue, sum = &sum,
         delay = m_delay_per_update]() mutable {
          auto market_update = MarketUpdate{};

          while (not token.stop_requested()) {
            queue.pop(market_update);
            // do some stuff with this
            *sum += market_update.price;
            auto target = std::chrono::steady_clock::now() + delay;

            while (std::chrono::steady_clock::now() < target) {
              // busy wait (or _mm_pause on x86)
            }
            static auto counter = 0;
            counter++;
            // if (counter % 500 == 0) {
            std::print("\r{:010.2f} Queue Size: {:<5}", *sum, queue.size());
            std::cout << std::flush;
            // };
          }
        });
  }

  auto stop_consuming() noexcept -> void {
    m_stop_source.request_stop();
    m_consuming_thread->join();
  }

private:
  void wait_hybrid() const {
    auto target = std::chrono::steady_clock::now() + m_delay_per_update;

    if (m_delay_per_update > std::chrono::microseconds(50)) {
      std::this_thread::sleep_for(m_delay_per_update -
                                  std::chrono::microseconds(50));
    }

    while (std::chrono::steady_clock::now() < target) {
    }
  }
  lf_queue_read m_queue;
  Duration m_delay_per_update;
  std::stop_source m_stop_source;
  std::unique_ptr<std::jthread> m_consuming_thread;

  double sum;
};
