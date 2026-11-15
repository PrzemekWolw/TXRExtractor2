#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace txr {

struct TexRef {
    uint32_t slot = 0;
    uint32_t index = 0;
};

struct Prim {
    std::string name;
    std::vector<uint32_t> indices;
    std::vector<TexRef> refs;
    std::vector<uint8_t> png;
    bool blend = false;
};

struct ShapeMesh {
    std::string name;
    std::vector<float> pos;
    std::vector<float> uv;
    std::vector<float> uv2;
    std::vector<float> uv3;
    std::vector<float> nrm;
    std::vector<float> col;
    std::vector<Prim> prims;
};

std::vector<uint8_t> buildGlb(const std::vector<ShapeMesh>& meshes);

} // namespace txr
