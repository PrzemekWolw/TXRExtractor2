#pragma once

#include "glb.hpp"
#include "png.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace txr {

struct WmnEntry {
    std::string name;
    uint64_t offset = 0;
    uint32_t stored = 0;
    uint32_t size = 0;
};

std::vector<WmnEntry> parseWmnToc(const uint8_t* toc, size_t n);
std::vector<uint8_t> inflateWmn(const uint8_t* dat, size_t n, size_t off, size_t fileSize);

struct WmnDecoded {
    std::vector<ShapeMesh> meshes;
    std::vector<std::pair<std::string, Image>> images;
};

WmnDecoded decodeGrid(const uint8_t* data, size_t n);

} // namespace txr
