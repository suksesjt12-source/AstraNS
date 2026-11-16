cmake_minimum_required(VERSION 3.22.1)
project(astrans_core LANGUAGES CXX)

add_library(astrans_core SHARED
    arm64_interpreter.cpp
    jni_bridge.cpp
)

target_include_directories(astrans_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(astrans_core PRIVATE cxx_std_20)
target_compile_options(astrans_core PRIVATE -Wall -Wextra -Wpedantic)
