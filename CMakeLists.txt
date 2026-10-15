cmake_minimum_required(VERSION 3.16)
project(BacktestOverfitDetector LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags for ultra-low latency statistical calculations
if(MSVC)
    add_compile_options(/O2 /W4)
else()
    add_compile_options(-O3 -Wall -Wextra -pthread)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(SOURCES
    src/main.cpp
    src/Statistics.cpp
)

add_executable(dsr_engine ${SOURCES})

# Target link libraries
if(WIN32)
    target_link_libraries(dsr_engine PRIVATE pq ws2_32 crypt32)
else()
    target_link_libraries(dsr_engine PRIVATE pq pthread)
endif()
