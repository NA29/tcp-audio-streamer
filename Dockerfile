# epoll is a Linux syscall, so the build and run happen in here, not on macOS.
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake gdb netcat-openbsd python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
CMD ["/bin/bash"]
