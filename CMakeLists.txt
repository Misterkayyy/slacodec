cmake_minimum_required(VERSION 3.16)

project(slacodec_core_mvp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Tests ─────────────────────────────────────────────────────
add_executable(slac_core_smoke
    tests/core_lossless_smoke.cpp
)

add_executable(slac_container_smoke
    tests/container_smoke.cpp
)

add_executable(slac_dsp_smoke
    tests/dsp_widening_smoke.cpp
)

add_executable(slac_reverb_registry_smoke
    tests/reverb_registry_smoke.cpp
)

add_executable(slac_reverb_conv_smoke
    tests/reverb_convolution_smoke.cpp
)

add_executable(slac_ir_diagnostic
    tests/ir_diagnostic.cpp
)

add_executable(slac_frame_smoke
    tests/frame_smoke.cpp
)

add_executable(slac_fuzz_smoke
    tests/fuzz_smoke.cpp
)

add_executable(slac_fdn_smoke
    tests/fdn_reverb_smoke.cpp
)

add_executable(slac_auto_chunk_smoke
    tests/auto_chunk_smoke.cpp
)

add_executable(slac_partitioned_conv_smoke
    tests/partitioned_conv_smoke.cpp
)

add_executable(slac_ring_buffer_smoke
    tests/ring_buffer_smoke.cpp
)

add_executable(slac_streaming_decoder_smoke
    tests/streaming_decoder_smoke.cpp
)

add_executable(slac_block_processor_smoke
    tests/block_processor_smoke.cpp
)

add_executable(slac_realtime_player_smoke
    tests/realtime_player_smoke.cpp
)

# ── CLI ───────────────────────────────────────────────────────
add_executable(slacodec-cli
    src/slac/cli/main.cpp
)

foreach(target IN ITEMS
    slac_core_smoke
    slac_container_smoke
    slac_dsp_smoke
    slac_reverb_registry_smoke
    slac_reverb_conv_smoke
    slacodec-cli
    slac_ir_diagnostic
    slac_frame_smoke
    slac_fuzz_smoke
    slac_fdn_smoke
    slac_auto_chunk_smoke
    slac_partitioned_conv_smoke
    slac_ring_buffer_smoke
    slac_streaming_decoder_smoke
    slac_block_processor_smoke
    slac_realtime_player_smoke
)
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endforeach()

option(SLAC_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(SLAC_BUILD_FUZZER "Build libFuzzer target (requires clang)" OFF)
if(SLAC_BUILD_FUZZER)
    add_executable(slac_fuzz tests/fuzz_target.cpp)
    target_include_directories(slac_fuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(slac_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(slac_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if(SLAC_ENABLE_SANITIZERS AND NOT MSVC)
    foreach(target IN ITEMS
        slac_core_smoke
        slac_container_smoke
        slac_dsp_smoke
        slac_reverb_registry_smoke
        slac_reverb_conv_smoke
        slacodec-cli
	slac_fuzz_smoke
	slac_fdn_smoke
	slac_auto_chunk_smoke
	slac_partitioned_conv_smoke
	slac_ring_buffer_smoke
	slac_streaming_decoder_smoke
        slac_block_processor_smoke
	slac_realtime_player_smoke
    )
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
        )
    endforeach()
endif()
