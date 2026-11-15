#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace txr {

struct UclError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::vector<uint8_t> decompressNrv2b(const uint8_t* data, size_t n, size_t expected);
std::vector<uint8_t> decompressUclpack(const uint8_t* data, size_t n);

} // namespace txr
