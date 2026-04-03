#include "client/lf_queue.hpp"
#include "common/types.hpp"
#include "network/udp_socket.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <stop_token>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

enum class ReaderState {
  Initialized,
  Reading,
};
template <ReaderState T> struct reader {
  using shared_lf_queue_write =
      std::shared_ptr<lf_queue<MarketUpdate, Permissions::Write>>;
  template <ReaderState> friend struct reader;

public:
  reader(reader<ReaderState::Initialized> &&other)
      : write_queue(std::move(other.write_queue)),
        // dont move socket cuz it was already moved into the thread
        reading_thread(std::move(other.reading_thread)) {}

  reader(shared_lf_queue_write write_queue,
         udp_socket<SocketType::Receiever> socket)
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
      return res;
    }

    std::println("Bound to port: {}", port);

    if (auto res = socket.join_multicast_group("239.255.0.1"); not res) {
      return res;
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
    if (kevent(kq, event, 1, NULL, 0, NULL) == -1) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }

    reading_thread = std::make_unique(
        [socket = std::move(socket), kq](std::stop_token stoken) {
          // setup callback for stopping
          std::stop_callback stop_cb(stoken, [kq]() {
            struct kevent trigger_ev;
            EV_SET(&trigger_ev, WAKEUP_EVENT_ID, EVFILT_USER, 0, NOTE_TRIGGER,
                   0, nullptr);
            kevent(kq, &trigger_ev, 1, nullptr, 0, nullptr);
          });

          struct kevent event_list[MAX_NUM_EVENTS];
          uint8_t buffer[sizeof(MarketUpdate)];
          for (;;) {
            const auto num_events =
                size_t(kevent(kq, NULL, 0, event_list, MAX_NUM_EVENTS, NULL));
            if (num_events == -1) {
              // failed to wait
              return std::unexpected(
                  std::error_code(errno, std::system_category()));
            }

            for (auto &event : std::span{event_list, num_events}) {
              if (event.ident == socket.native_handle()) {
                auto sender_addr = sockaddr_in{};
                auto sender_len = socklen_t(sizeof(sender_addr));

                auto bytes_read = recvfrom(
                    socket.native_handle(), buffer, 1024, 0,
                    reinterpret_cast<sockaddr *>(&sender_addr), &sender_len);

                if (bytes_read > 0) {
                  const auto address = inet_ntoa(sender_addr.sin_addr);
                  const auto port = ntohs(sender_addr.sin_port);
                  // reinterpret as marketupdate
                  const auto market_update =
                      reinterpret_cast<const MarketUpdate *>(&buffer);

                  std::println("Received update with price: {} from {}:{}",
                               market_update->price, address, port);

                } else if (bytes_read < 0) {
                  std::println(stderr, "Failed to read");
                }
              }
            }
          }
        });

    return this;
  }

  auto stop_reading() -> void { this->reading_thread->request_stop(); }

private:
  operator reader<ReaderState::Reading>()
    requires(T == ReaderState::Initialized)
  {
    return reader<ReaderState::Reading>(std::move(*this));
  }
  shared_lf_queue_write write_queue;
  udp_socket<SocketType::Receiever> socket;

  std::unique_ptr<std::jthread> reading_thread;

  constexpr static uintptr_t WAKEUP_EVENT_ID = 1;

  constexpr static auto MAX_NUM_EVENTS = 100uz;
};
