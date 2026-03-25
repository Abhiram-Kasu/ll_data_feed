

#include "udp_socket.hpp"
#include <fcntl.h>

struct multicast {

public:
private:
  auto set_nonblocking(udp_socket::sockfd socket_fd)
      -> std::expected<void, std::error_code>;
};
