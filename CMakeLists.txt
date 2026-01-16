cmake_minimum_required(VERSION 3.12)
project(tcp-audio-streamer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(serverApp src/server/main.cpp)
add_executable(clientApp src/client/main.cpp)

target_include_directories(serverApp PRIVATE include)
target_include_directories(clientApp PRIVATE include)
