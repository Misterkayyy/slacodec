cmake_minimum_required(VERSION 3.22.1)
project("slacodec_jni")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Biblioteca Compartilhada JNI ──────────────────────────────
# Como o core é header-only, apenas compilamos o arquivo de bindings JNI.
add_library(slacodec_jni SHARED
    slacodec_jni.cpp
)

# ── Includes do Core SLACodec ─────────────────────────────────
# Aponta para a raiz src/ do seu repositório original.
target_include_directories(slacodec_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src
)

# ── Otimizações NEON (ARM64) ──────────────────────────────────
if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    target_compile_options(slacodec_jni PRIVATE
        -O3
        -funroll-loops
        # -ffast-math pode ser útil para DSP/Reverb, mas valide se quebra 
        # o bit-exactness do core lossless. Se quebrar, remova esta flag.
        -ffast-math 
    )
endif()

# ── Linkagem com Bibliotecas do Sistema Android ───────────────
find_library(log-lib log)
find_library(aaudio-lib aaudio)
find_library(android-lib android)

target_link_libraries(slacodec_jni PRIVATE
    ${log-lib}
    ${aaudio-lib}
    ${android-lib}
)
