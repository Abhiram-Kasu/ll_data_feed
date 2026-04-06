#include "client/lf_queue.hpp"
#include "common/types.hpp"
#include "network/udp_socket.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <print>
#include <ranges>
#include <stop_token>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

enum class ReaderState {
  Initialized,
  Reading,
};
struct diag_internal {
  std::atomic<uint64_t> num_received;
  std::atomic<uint64_t> num_dropped;
};
struct diag {
  uint64_t num_dropped, num_received;
};
template <ReaderState T> struct reader {
  using lf_queue_write = lf_queue<MarketUpdate, Permissions::Write>;
  template <ReaderState> friend struct reader;

private:
public:
  reader(reader<ReaderState::Initialized> &&other)
      : write_queue(std::move(other.write_queue)),
        socket(std::move(other.socket)),
        // socket was already moved into the thread, so this just moves the
        // empty shell
        diagnostics(std::move(other.diagnostics)),
        reading_thread(std::move(other.reading_thread)) {}

  reader(lf_queue_write write_queue, udp_socket<SocketType::Receiever> socket)
    requires(T == ReaderState::Initialized)
      : write_queue(write_queue), socket(std::move(socket))

  {}

  auto start_reading(uint16_t port)
      -> std::expected<reader<ReaderState::Reading>, std::error_code>
    requires(T == ReaderState::Initialized)
  {

    // bind the udp socket
    auto res = socket.bind(port);
    if (not res.has_value()) {
      return std::unexpected(res.error());
    }

    std::println("Bound to port: {}", port);

    if (auto res = socket.join_multicast_group("239.255.0.1"); not res) {
      return std::unexpected(res.error());
    }

    std::println("Listening for packets");

    // implement kqueue here
    auto kq = kqueue();
    if (kq == -1) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }

    // first we need to register the socket with kqueue to listen for read
    // events
    struct kevent event[2];
    EV_SET(&event[0], socket.native_handle(), EVFILT_READ, EV_ADD | EV_ENABLE,
           0, 0, nullptr);

    EV_SET(&event[1], WAKEUP_EVENT_ID, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0,
           nullptr);
    // apply changes to our kqueue
    if (kevent(kq, event, 2, NULL, 0, NULL) == -1) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }

    auto diagnostics_copy = auto(this->diagnostics);

    reading_thread = std::make_unique<std::jthread>(
        [socket = std::move(socket), kq, diagnostics_copy,
         write_queue_copy =
             write_queue](const std::stop_token &stoken) mutable {
          // setup callback for stopping
          std::stop_callback stop_cb(stoken, [kq]() {
            std::println("Sending Wakeup Event");
            struct kevent trigger_ev;
            EV_SET(&trigger_ev, WAKEUP_EVENT_ID, EVFILT_USER, 0, NOTE_TRIGGER,
                   0, nullptr);
            kevent(kq, &trigger_ev, 1, nullptr, 0, nullptr);
          });

          struct kevent event_list[MAX_NUM_EVENTS];
          uint8_t buffer[sizeof(MarketUpdate)];

          auto last_counter_seen = 0z;

          for (;;) {

            if (stoken.stop_requested()) {
              std::println("ACK stop requested and breaking");
              break;
            }
            const auto num_events =
                size_t(kevent(kq, NULL, 0, event_list, MAX_NUM_EVENTS, NULL));
            if (num_events == -1) {
              // failed to wait
              break;
            }

            for (auto &event : std::span{event_list, num_events}) {

              if (event.ident == WAKEUP_EVENT_ID) {
                // We were woken up by the stop_callback.
                // The loop will go back to the top, see stop_requested(), and
                // break.
                std::println("Received close request from kq");
                continue;
              } else if (event.ident == socket.native_handle()) {
                auto sender_addr = sockaddr_in{};
                auto sender_len = socklen_t(sizeof(sender_addr));

                auto bytes_read = recvfrom(
                    socket.native_handle(), buffer, sizeof(MarketUpdate), 0,
                    reinterpret_cast<sockaddr *>(&sender_addr), &sender_len);

                if (bytes_read > 0) {
                  const auto address = inet_ntoa(sender_addr.sin_addr);
                  const auto port = ntohs(sender_addr.sin_port);
                  // reinterpret as marketupdate
                  auto *market_update =
                      reinterpret_cast<MarketUpdate *>(&buffer);

                  if (market_update->seq != last_counter_seen + 1) {
                    diagnostics_copy->num_dropped.fetch_add(
                        1, std::memory_order_relaxed);
                  }
                  last_counter_seen = market_update->seq;

                  write_queue_copy.push(std::move(*market_update));
                  diagnostics_copy->num_received.fetch_add(
                      1, std::memory_order_relaxed);

                } else if (bytes_read < 0) {
                  std::println(stderr, "Failed to read");
                }
              }
            }
          }
          std::println("Closing kq");
          close(kq);
        });

    return static_cast<reader<ReaderState::Reading>>(std::move(*this));
  }

  [[nodiscard]] auto get_diag() const noexcept -> diag {
    return diag{diagnostics->num_dropped.load(std::memory_order_acquire),
                diagnostics->num_received.load(std::memory_order_acquire)};
  }

  auto stop_reading() noexcept -> void { this->reading_thread->request_stop(); }

  auto join() noexcept -> void { reading_thread->join(); }

private:
  operator reader<ReaderState::Reading>()
    requires(T == ReaderState::Initialized)
  {
    return reader<ReaderState::Reading>(std::move(*this));
  }
  lf_queue_write write_queue;
  udp_socket<SocketType::Receiever> socket;

  std::unique_ptr<std::jthread> reading_thread;

  constexpr static uintptr_t WAKEUP_EVENT_ID = 1;

  constexpr static auto MAX_NUM_EVENTS = 100uz;

  std::shared_ptr<diag_internal> diagnostics =
      std::make_shared<diag_internal>();
};
