#include "server/server.h"

#include <cstdio>

int main(int argc, char **argv) {
  // Line-buffer stdout. When stdout is a pipe (docker logs, journald, a test harness)
  // it defaults to 4KB block buffering, so logs appear late or vanish entirely if the
  // process is killed. Servers want logs on the line, not on the buffer flush.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  Server server;
  server.run(argc > 1 ? argv[1] : nullptr);
  return 0;
}
