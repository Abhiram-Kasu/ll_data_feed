#pragma once
#include <arpa/inet.h>
#include <cerrno>
#include <expected>
#include <istream>
#include <netinet/in.h>
#include <print>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct udp_socket {
public:
  using sockfd = int;
  static auto try_create() noexcept
      -> std::expected<udp_socket, std::error_code>;

  ~udp_socket();
  // copy ctor
  udp_socket(const udp_socket &socket) = delete;
  // copy assingment operator
  auto operator=(const udp_socket &socket) -> udp_socket & = delete;
  // move contructor
  udp_socket(udp_socket &&socket) noexcept;
  // move assignment operator
  auto operator=(udp_socket &&socket) noexcept -> udp_socket &;

  auto native_handle() const noexcept -> sockfd;

  auto set_nonblocking() noexcept -> std::expected<void, std::error_code>;

  auto set_socket_reuse() noexcept -> std::expected<void, std::error_code>;

private:
  explicit udp_socket(sockfd fd) noexcept;

  sockfd m_fd;
};
