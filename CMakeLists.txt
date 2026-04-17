cmake_minimum_required(VERSION 3.20)
project(DNES LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(third_party/SDL)

add_executable(dnes
    src/main.cpp
    src/apu.cpp
    src/bus.cpp
    src/cartridge.cpp
    src/cpu6502.cpp
    src/cpu6502_trace.cpp
    src/cpuRam.cpp
    src/mapper.cpp
    src/nes.cpp
    src/ppu.cpp
    src/SDLScreen.cpp
)

target_include_directories(dnes PRIVATE include)

target_link_libraries(dnes PRIVATE SDL3::SDL3)