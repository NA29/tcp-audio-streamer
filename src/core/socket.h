class Socket {
private:
  int fd;

public:
  Socket();
  // otherwise any int can become a Socket implicitly (UB)
  // ex: handleTask(Sockets) will not be flagged by compiler
  explicit Socket(int fd);
  ~Socket();

  // important for RAII!!, prevents two objects from accessing the same resource
  // copy operator : creating a socker from another one
  Socket(const Socket &) = delete;
  // copy assignment operator : assigning a socker into an existing socket
  Socket &operator=(const Socket &) = delete;
};
