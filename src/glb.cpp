#include "glb.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

namespace txr {
namespace {

void appendPad(std::vector<uint8_t>& bin, const void* data, size_t n, size_t& offset, int& view, nlohmann::json& views,
               int target)
{
    offset = bin.size();
    bin.insert(bin.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + n);
    while (bin.size() & 3) {
        bin.push_back(0);
    }
    nlohmann::json v = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", n}};
    if (target) {
        v["target"] = target;
    }
    views.push_back(std::move(v));
    view = int(views.size()) - 1;
}

int addAccessor(std::vector<uint8_t>& bin, nlohmann::json& views, nlohmann::json& accs, const void* data, size_t n,
                int component, const char* type, size_t count, int target, const nlohmann::json* minv,
                const nlohmann::json* maxv)
{
    size_t offset = 0;
    int view = 0;
    appendPad(bin, data, n, offset, view, views, target);
    nlohmann::json a = {{"bufferView", view}, {"componentType", component}, {"count", count}, {"type", type}};
    if (minv) {
        a["min"] = *minv;
    }
    if (maxv) {
        a["max"] = *maxv;
    }
    accs.push_back(std::move(a));
    return int(accs.size()) - 1;
}

} // namespace

std::vector<uint8_t> buildGlb(const std::vector<ShapeMesh>& meshes)
{
    nlohmann::json views = nlohmann::json::array();
    nlohmann::json accs = nlohmann::json::array();
    nlohmann::json gltfMeshes = nlohmann::json::array();
    nlohmann::json materials = nlohmann::json::array();
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json images = nlohmann::json::array();
    nlohmann::json textures = nlohmann::json::array();
    nlohmann::json sceneNodes = nlohmann::json::array();
    std::vector<uint8_t> bin;
    std::map<std::string, int> matIndex;
    std::map<size_t, int> pngIndex;

    auto materialOf = [&](const Prim& prim) {
        size_t pngHash = 0;
        if (!prim.png.empty()) {
            pngHash = std::hash<std::string>{}(std::string(reinterpret_cast<const char*>(prim.png.data()), prim.png.size()));
        }
        const std::string nameKey = prim.name + "|" + std::to_string(pngHash) + (prim.blend ? "|b" : "");
        auto it = matIndex.find(nameKey);
        if (it != matIndex.end()) {
            return it->second;
        }
        nlohmann::json pbr = {{"baseColorFactor", {1.0, 1.0, 1.0, 1.0}}, {"metallicFactor", 0.0}, {"roughnessFactor", 0.9}};
        if (!prim.png.empty()) {
            auto found = pngIndex.find(pngHash);
            int tex = 0;
            if (found == pngIndex.end()) {
                size_t off = 0;
                int view = 0;
                appendPad(bin, prim.png.data(), prim.png.size(), off, view, views, 0);
                images.push_back({{"bufferView", view}, {"mimeType", "image/png"}});
                textures.push_back({{"source", int(images.size()) - 1}});
                tex = int(textures.size()) - 1;
                pngIndex[pngHash] = tex;
            } else {
                tex = found->second;
            }
            pbr["baseColorTexture"] = {{"index", tex}};
        }
        nlohmann::json mat = {{"name", prim.name}, {"pbrMetallicRoughness", pbr}};
        if (prim.blend) {
            mat["alphaMode"] = "BLEND";
        }
        materials.push_back(std::move(mat));
        const int index = int(materials.size()) - 1;
        matIndex[nameKey] = index;
        return index;
    };

    for (const ShapeMesh& mesh : meshes) {
        const size_t verts = mesh.pos.size() / 3;
        if (verts == 0) {
            continue;
        }
        float minv[3] = {mesh.pos[0], mesh.pos[1], mesh.pos[2]};
        float maxv[3] = {minv[0], minv[1], minv[2]};
        for (size_t i = 0; i < verts; ++i) {
            for (int c = 0; c < 3; ++c) {
                minv[c] = std::min(minv[c], mesh.pos[i * 3 + c]);
                maxv[c] = std::max(maxv[c], mesh.pos[i * 3 + c]);
            }
        }
        const nlohmann::json jmin = {minv[0], minv[1], minv[2]};
        const nlohmann::json jmax = {maxv[0], maxv[1], maxv[2]};
        const int pacc = addAccessor(bin, views, accs, mesh.pos.data(), mesh.pos.size() * 4, 5126, "VEC3", verts, 34962,
                                     &jmin, &jmax);
        nlohmann::json attrs = {{"POSITION", pacc}};
        auto vec2 = [&](const std::vector<float>& v, const char* sem) {
            if (v.size() < 2) {
                return;
            }
            const int a = addAccessor(bin, views, accs, v.data(), v.size() * 4, 5126, "VEC2", v.size() / 2, 34962, nullptr,
                                      nullptr);
            attrs[sem] = a;
        };
        vec2(mesh.uv, "TEXCOORD_0");
        vec2(mesh.uv2, "TEXCOORD_1");
        vec2(mesh.uv3, "TEXCOORD_2");
        if (mesh.nrm.size() >= 3) {
            attrs["NORMAL"] = addAccessor(bin, views, accs, mesh.nrm.data(), mesh.nrm.size() * 4, 5126, "VEC3",
                                          mesh.nrm.size() / 3, 34962, nullptr, nullptr);
        }
        if (mesh.col.size() >= 3) {
            attrs["COLOR_0"] = addAccessor(bin, views, accs, mesh.col.data(), mesh.col.size() * 4, 5126, "VEC3",
                                           mesh.col.size() / 3, 34962, nullptr, nullptr);
        }
        nlohmann::json prims = nlohmann::json::array();
        for (const Prim& prim : mesh.prims) {
            if (prim.indices.size() < 3) {
                continue;
            }
            bool bad = false;
            for (uint32_t id : prim.indices) {
                if (id >= verts) {
                    bad = true;
                    break;
                }
            }
            if (bad) {
                continue;
            }
            const int iacc = addAccessor(bin, views, accs, prim.indices.data(), prim.indices.size() * 4, 5125, "SCALAR",
                                         prim.indices.size(), 34963, nullptr, nullptr);
            prims.push_back({{"attributes", attrs},
                             {"indices", iacc},
                             {"material", materialOf(prim)},
                             {"mode", 4}});
        }
        if (prims.empty()) {
            continue;
        }
        gltfMeshes.push_back({{"name", mesh.name}, {"primitives", prims}});
        nodes.push_back({{"mesh", int(gltfMeshes.size()) - 1}, {"name", mesh.name}});
        sceneNodes.push_back(int(nodes.size()) - 1);
    }

    nlohmann::json gltf = {{"asset", {{"version", "2.0"}, {"generator", "TXRExtractor2"}}},
                           {"scene", 0},
                           {"scenes", {{{"nodes", sceneNodes}}}},
                           {"nodes", nodes},
                           {"meshes", gltfMeshes},
                           {"accessors", accs},
                           {"bufferViews", views},
                           {"buffers", {{{"byteLength", bin.size()}}}}};
    if (!materials.empty()) {
        gltf["materials"] = materials;
    }
    if (!images.empty()) {
        gltf["images"] = images;
        gltf["textures"] = textures;
    }
    std::string json = gltf.dump();
    while (json.size() & 3) {
        json.push_back(' ');
    }
    const uint32_t jsonLen = uint32_t(json.size());
    const uint32_t binLen = uint32_t(bin.size());
    const uint32_t total = 12 + 8 + jsonLen + 8 + binLen;
    std::vector<uint8_t> out(total);
    std::memcpy(out.data(), "glTF", 4);
    const uint32_t version = 2;
    std::memcpy(out.data() + 4, &version, 4);
    std::memcpy(out.data() + 8, &total, 4);
    std::memcpy(out.data() + 12, &jsonLen, 4);
    const uint32_t jsonType = 0x4E4F534A;
    std::memcpy(out.data() + 16, &jsonType, 4);
    std::memcpy(out.data() + 20, json.data(), jsonLen);
    std::memcpy(out.data() + 20 + jsonLen, &binLen, 4);
    const uint32_t binType = 0x004E4942;
    std::memcpy(out.data() + 24 + jsonLen, &binType, 4);
    if (!bin.empty()) {
        std::memcpy(out.data() + 28 + jsonLen, bin.data(), bin.size());
    }
    return out;
}

} // namespace txr
