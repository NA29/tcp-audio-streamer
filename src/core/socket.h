class Socket {
private:
  int fd_;

public:
  Socket();

  // otherwise any int can become a Socket implicitly (UB)
  // ex: handleTask(Sockets) will not be flagged by compiler
  explicit Socket(int fd);

  ~Socket();

  // important for RAII!!, prevents two objects from accessing the same resource
  // copy constructor : creating a socker from another one (ref)
  Socket(const Socket &) = delete;

  // copy assignment operator : assigning a socker into an existing socket
  Socket &operator=(const Socket &) = delete;

  // move constructor
  Socket(Socket &&) noexcept;

  // move assignment operator
  Socket &operator=(Socket &&) noexcept;
};
