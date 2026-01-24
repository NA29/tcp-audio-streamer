cmake_minimum_required(VERSION 3.12)
project(tcp-audio-streamer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# if this is not on neovim lsp stops workign for some reason
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(serverApp 
  src/server/main.cpp
  src/server/server.cpp
  src/core/socket.cpp
)
add_executable(clientApp 
  src/client/main.cpp
  src/core/socket.cpp
)

target_include_directories(serverApp PRIVATE src)
target_include_directories(clientApp PRIVATE src)
