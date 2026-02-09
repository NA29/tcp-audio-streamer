#include "server/server.h"
#include "core/socket.h"
#include "utils/log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h> // POSIX socket API
#include <unistd.h>

void handle_client(Socket &client);

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

  while (true) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int client_fd =
        ::accept(listen_socket.fd(), reinterpret_cast<sockaddr *>(&client_addr),
                 &client_len);

    if (client_fd < 0) {
      LOG_ERROR("accept() failed");
      continue;
    }

    Socket client_socket(client_fd);
    LOG_INFO("client connected");
    handle_client(client_socket);
  }
}

// handling recv()
void handle_client(Socket &client) {
  char buffer[4096];

  while (true) {
    ssize_t n = ::recv(client.fd(), buffer, sizeof(buffer), 0);

    if (n > 0) {
      LOG_INFO("received %zd bytes", n);
    } else if (n == 0) {
      LOG_INFO("client disconnected");
      break;
    } else {
      LOG_ERROR("recv() failed");
      break;
    }
  }
}

// blocking send (partial writes)
bool send_all(Socket &socket, const void *data, size_t size) {
  const char *buf = static_cast<const char *>(data);
  size_t sent = 0;

  while (sent < size) {
    ssize_t n = ::send(socket.fd(), buf + sent, size - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += n;
  }
  return true;
}
