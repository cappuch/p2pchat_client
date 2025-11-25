cmake_minimum_required(VERSION 3.15)
project(p2pchat_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

add_subdirectory(lib-p2pchat)

add_executable(chat_client src/client.cpp)
target_link_libraries(chat_client PRIVATE p2pchat ftxui::screen ftxui::dom ftxui::component)

add_executable(discovery_server src/discovery_server.cpp)
find_package(Threads REQUIRED)
target_link_libraries(discovery_server PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(discovery_server PRIVATE ws2_32)
endif()
