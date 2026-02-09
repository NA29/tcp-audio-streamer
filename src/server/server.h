#pragma once
#include <cstdint>

class Server {
public:
  void run();
  static constexpr uint16_t kPort = 12345;
  static constexpr int kListenBacklog = 5;
  static constexpr size_t kRecvBufferSize = 4096;
};
