#include <slac/core/container.hpp>

#include <cstdint>
#include <cstddef>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    try {
        slac::HashInfo hash;
        auto pcm = slac::decodeSlacFile(
            std::vector<uint8_t>(data, data + size), nullptr, nullptr, nullptr, &hash);
        (void)pcm;
    } catch (...) {
    }
    return 0;
}
