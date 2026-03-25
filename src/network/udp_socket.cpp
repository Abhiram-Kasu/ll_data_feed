#include "udp_socket.hpp"
#include <__expected/unexpect.h>
#include <__expected/unexpected.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <system_error>

udp_socket::udp_socket(sockfd fd) noexcept : m_fd(fd) {}

udp_socket::~udp_socket() {
  if (m_fd >= 0) {
    if (close(m_fd) < 0) {
      std::println(stderr, "failed to close socket with sockfd: {}", m_fd);
    };
  }
}

auto udp_socket::try_create() noexcept
    -> std::expected<udp_socket, std::error_code> {
  sockfd fd;
  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    return std::unexpected{std::error_code{errno, std::system_category()}};
  }
  return udp_socket{fd};
}

auto udp_socket::operator=(udp_socket &&socket) noexcept -> udp_socket & {
  if (this != &socket) {

    if (m_fd >= 0) {
      close(m_fd);
    }
    m_fd = socket.m_fd;
    socket.m_fd = -1;
  }
  return *this;
}

udp_socket::udp_socket(udp_socket &&socket) noexcept : m_fd(socket.m_fd) {
  socket.m_fd = -1;
}
auto udp_socket::native_handle() const noexcept -> sockfd { return m_fd; }

auto udp_socket::set_nonblocking() noexcept
    -> std::expected<void, std::error_code> {
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
auto udp_socket::set_socket_reuse() noexcept
    -> std::expected<void, std::error_code> {
  auto option = 1;
  if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
    return std::unexpected{std::error_code(errno, std::system_category())};
  }
  return {};
}
