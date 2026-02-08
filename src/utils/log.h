#pragma once
#include <cstdio>

#define LOG_INFO(fmt, ...)                                                     \
  do {                                                                         \
    std::fprintf(stdout, "[INFO] %s %d: " fmt "\n", __FILE__, __LINE__,        \
                 ##__VA_ARGS__);                                               \
  } while (0)

#define LOG_ERROR(fmt, ...)                                                    \
  do {                                                                         \
    std::fprintf(stderr, "[ERROR] %s %d: " fmt "\n", __FILE__, __LINE__,       \
                 ##__VA_ARGS__);                                               \
  } while (0)
