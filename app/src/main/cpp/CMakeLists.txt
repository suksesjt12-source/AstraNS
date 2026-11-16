cmake_minimum_required(VERSION 3.22.1)
project(astrans_core LANGUAGES CXX)

add_library(astrans_core SHARED
    arm64_interpreter.cpp
    nro_loader.cpp
    nso_loader.cpp
    mod0_relocator.cpp
    hipc_cmif.cpp
    guest_address_space.cpp
    jni_bridge.cpp
    vulkan_probe.cpp
)

target_include_directories(astrans_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(astrans_core PRIVATE cxx_std_20)
target_compile_options(astrans_core PRIVATE -Wall -Wextra -Wpedantic)

find_library(VULKAN_LIBRARY vulkan)
if(VULKAN_LIBRARY)
    target_compile_definitions(astrans_core PRIVATE ASTRA_HAS_VULKAN=1)
    target_link_libraries(astrans_core PRIVATE ${VULKAN_LIBRARY})
else()
    target_compile_definitions(astrans_core PRIVATE ASTRA_HAS_VULKAN=0)
endif()
