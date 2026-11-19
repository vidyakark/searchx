cmake_minimum_required(VERSION 3.14)
project(searchx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(Threads REQUIRED)

add_library(searchx_core STATIC
  src/tokenizer.cpp src/index.cpp src/query.cpp src/engine.cpp)
target_include_directories(searchx_core PUBLIC include)
target_link_libraries(searchx_core PUBLIC Threads::Threads)
target_compile_options(searchx_core PRIVATE -Wall -Wextra)

add_executable(searchx src/main.cpp src/server.cpp)
target_link_libraries(searchx PRIVATE searchx_core)

add_executable(gen_corpus tools/gen_corpus.cpp)
add_executable(bench tools/bench.cpp)
target_link_libraries(bench PRIVATE searchx_core)

enable_testing()
add_executable(run_tests tests/test_main.cpp)
target_link_libraries(run_tests PRIVATE searchx_core)
add_test(NAME unit COMMAND run_tests)
