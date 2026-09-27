cmake_minimum_required(VERSION 3.22.1)
project("slacodec_jni")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(slacodec_jni SHARED slacodec_jni.cpp)

# À PROVA DE FALHAS: Adiciona ambos os caminhos possíveis de include
target_include_directories(slacodec_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/slac
)

if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    target_compile_options(slacodec_jni PRIVATE -O3 -funroll-loops)
endif()

find_library(log-lib log)
find_library(aaudio-lib aaudio)
target_link_libraries(slacodec_jni PRIVATE ${log-lib} ${aaudio-lib} android)
