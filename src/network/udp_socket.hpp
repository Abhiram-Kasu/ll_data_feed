#pragma once
#include <arpa/inet.h>

#include <cerrno>
#include <cstddef>
#include <expected>
#include <netinet/in.h>
#include <print>
#include <span>
#include <sys/socket.h>

#include <netinet/in.h>
#include <sys/_endian.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>

#include <unistd.h>

enum class SocketType { Sender, Receiever };

template <SocketType Type>
concept Sender = (Type == SocketType::Sender);

template <SocketType Type>
concept Receiver = (Type == SocketType::Receiever);

template <SocketType Type> struct udp_socket {
public:
  using sockfd = int;

  static auto try_create() noexcept
      -> std::expected<udp_socket, std::error_code> {
    sockfd fd;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return udp_socket<Type>{fd};
  }

  ~udp_socket() {
    if (m_fd >= 0) {
      if (close(m_fd) < 0) {
        std::println(stderr, "failed to close socket with sockfd: {}", m_fd);
      };
    }
  }
  // copy ctor
  udp_socket(const udp_socket &socket) = delete;
  // copy assingment operator
  auto operator=(const udp_socket &socket) -> udp_socket & = delete;
  // move contructor
  udp_socket(udp_socket<Type> &&socket) noexcept : m_fd(socket.m_fd) {
    socket.m_fd = -1;
  }
  // move assignment operator

  auto operator=(udp_socket<Type> &&socket) noexcept -> udp_socket & {
    if (this != &socket) {

      if (m_fd >= 0) {
        close(m_fd);
      }
      m_fd = socket.m_fd;
      socket.m_fd = -1;
    }
    return *this;
  }

  auto native_handle() const noexcept -> sockfd { return m_fd; }

  auto set_nonblocking() noexcept -> std::expected<void, std::error_code> {

    auto flags = fcntl(m_fd, F_GETFL);

    if (flags < 0) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }
    if (fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }

    return {};
  }
  // socket packets duplicated for each process listening on the port.
  //  Needed for udp mutlicast
  template <SocketType T = Type>
    requires Sender<T> or Receiver<T>
  auto set_socket_reuse() noexcept -> std::expected<void, std::error_code> {
    auto option = 1;
    if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) <
        0) {
      return std::unexpected{std::error_code(errno, std::system_category())};
    }

// needed for macos
#ifdef SO_REUSEPORT
    if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option)) <
        0) {
      return std::unexpected{std::error_code(errno, std::system_category())};
    }
#endif
    return {};
  }
  template <SocketType T = Type>
    requires Receiver<T>
  auto bind(uint16_t port) noexcept -> std::expected<void, std::error_code> {
    sockaddr_in address{.sin_family = AF_INET,
                        .sin_port = htons(port),
                        .sin_addr = {.s_addr = htonl(INADDR_ANY)}};

    if (::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
        0) {
      return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
  }

  auto write(std::span<const uint8_t> data, const sockaddr_in &addr) noexcept
      -> std::expected<size_t, std::error_code>
    requires Sender<Type>
  {
    auto res = sendto(m_fd, data.data(), data.size_bytes(), 0,
                      reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    if (res < 0) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }
    return static_cast<size_t>(res);
  }

  auto join_multicast_group(std::string_view multicast_ip) noexcept
      -> std::expected<void, std::error_code>
    requires Receiver<Type>
  {
    const auto mreq =
        ip_mreq{.imr_multiaddr = {.s_addr = inet_addr(multicast_ip.data())},
                .imr_interface = {.s_addr = htonl(INADDR_ANY)}};

    // set ip level options
    if (setsockopt(m_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) <
        0) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }

    return {};
  }

  auto read(std::span<uint8_t> buffer) noexcept
      -> std::expected<size_t, std::error_code>
    requires Receiver<Type>
  {
    auto res =
        recvfrom(m_fd, buffer.data(), buffer.size_bytes(), 0, nullptr, nullptr);
    if (res < 0) {
      return std::unexpected(std::error_code(errno, std::system_category()));
    }
    return static_cast<size_t>(res);
  }

private:
  explicit udp_socket(sockfd fd) noexcept : m_fd(fd) {}

  sockfd m_fd;
};
