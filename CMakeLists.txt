cmake_minimum_required(VERSION 3.20)

project(MyDrive LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS system)

add_library(
    mydrive_common
    src/common/checksum.cpp
    src/common/config.cpp
    src/common/file_utils.cpp
    src/common/protocol.cpp
    src/common/sync_logic.cpp
)

target_include_directories(mydrive_common PUBLIC src)
target_link_libraries(mydrive_common PUBLIC Boost::system)
target_compile_definitions(mydrive_common PUBLIC BOOST_ASIO_NO_DEPRECATED)

add_executable(
    mydrive_server
    src/server/main.cpp
    src/server/server_app.cpp
)
target_link_libraries(mydrive_server PRIVATE mydrive_common)

add_executable(
    mydrive_client
    src/client/main.cpp
    src/client/client_app.cpp
)
target_link_libraries(mydrive_client PRIVATE mydrive_common)

enable_testing()

add_executable(
    mydrive_tests
    tests/test_main.cpp
)
target_link_libraries(mydrive_tests PRIVATE mydrive_common)

add_test(NAME mydrive_tests COMMAND mydrive_tests)
