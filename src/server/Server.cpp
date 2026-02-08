#include "server/server.h"
#include "core/socket.h"
#include "utils/log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h> // POSIX socket API
#include <unistd.h>

void Server::run() {
  LOG_INFO("server starting");

  // creating socket
  auto socket_result = Socket::create();
  if (!socket_result) {
    LOG_ERROR("socket() failed: %s", socket_result.error().message.c_str());
    return;
  }

  Socket listen_socket = std::move(socket_result.value());
  LOG_INFO("SOCKET CREATED");

  // binding socket
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(kPort);

  if (::bind(listen_socket.fd(), reinterpret_cast<sockaddr *>(&addr),
             sizeof(addr)) < 0) {
    LOG_ERROR("bind() failed");
    return;
  }

  LOG_INFO("bound to port %d", kPort);

  // listen to socket
  if (::listen(listen_socket.fd(), 5) < 0) {
    LOG_ERROR("listen() failed");
    return;
  }

  LOG_INFO("LISTENING...");
}
