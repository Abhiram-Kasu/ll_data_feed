#include "network/udp_socket.hpp"
#include <atomic>
#include <memory>
#include <netinet/in.h>
#include <semaphore>
struct publisher {
public:
  // delete copy and move ctor
  auto operator=(publisher &&) noexcept = delete;
  auto operator=(const publisher &) noexcept = delete;

  publisher(const publisher &) noexcept = delete;
  publisher(publisher &&) noexcept = delete;

  publisher(uint64_t numMessagesPerSecond, uint64_t m_burst_size,
            udp_socket<SocketType::Sender> &&socket, sockaddr_in addr)
      : m_sending_socket(std::move(socket)),
        m_num_msg_per_second(numMessagesPerSecond), m_destination_address(addr),
        m_burst_size(m_burst_size) {}
  auto start() -> void;
  auto stop() -> void;

private:
  udp_socket<SocketType::Sender> m_sending_socket;
  uint64_t m_num_msg_per_second, m_burst_size;
  sockaddr_in m_destination_address;

  std::atomic<bool> m_stop_token{false};
};
