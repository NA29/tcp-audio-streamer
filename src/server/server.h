#pragma once
#include <cstdint>

class Server {
public:
  void run();

private:
  static constexpr uint16_t kPort = 12345;
};
