#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace txr {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

std::vector<uint8_t> encodePng(const Image& image);
bool mostlyTransparent(const Image& image);

} // namespace txr
