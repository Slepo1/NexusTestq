cmake_minimum_required(VERSION 3.13)
project(nexus_transmitter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_executable(nexus_transmitter
    src/main.cpp
    src/common/Log.cpp
    src/common/Crc32.cpp
    src/common/TimeUtil.cpp
    src/common/Paths.cpp
    src/transport/Destination.cpp
    src/transport/TcpTransport.cpp
    src/transport/Factory.cpp
    src/protocol/Protocol.cpp
    src/protocol/FrameParser.cpp
    src/server/FileWriter.cpp
    src/server/Session.cpp
    src/server/Server.cpp
    src/client/Client.cpp
)

target_include_directories(nexus_transmitter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(nexus_transmitter PRIVATE -Wall -Wextra -Wpedantic)