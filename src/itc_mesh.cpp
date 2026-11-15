#include "itc_mesh.hpp"

#include "bytes.hpp"
#include "xpr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>

namespace txr {
namespace {

constexpr size_t kPart = 0x20;
constexpr uint32_t kVersion = 0x00150000;
constexpr int kShapeBytes = 544;
constexpr int kMatBytes = 512;

float dist3(const std::vector<float>& pos, uint32_t a, uint32_t b)
{
    const float dx = pos[size_t(a) * 3] - pos[size_t(b) * 3];
    const float dy = pos[size_t(a) * 3 + 1] - pos[size_t(b) * 3 + 1];
    const float dz = pos[size_t(a) * 3 + 2] - pos[size_t(b) * 3 + 2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::vector<std::vector<uint32_t>> splitLong(const std::vector<float>& pos, const std::vector<uint32_t>& g)
{
    if (g.size() <= 4) {
        return {g};
    }
    const int n = int(g.size());
    int bestK = -1;
    float best = -1;
    for (int k : {3, 4}) {
        if (k < n && (n - k == 3 || n - k == 4)) {
            const float cross = dist3(pos, g[size_t(k - 1)], g[size_t(k)]);
            float left = 0;
            float right = 0;
            for (int i = 0; i < k - 1; ++i) {
                left = std::max(left, dist3(pos, g[size_t(i)], g[size_t(i + 1)]));
            }
            for (int i = k; i < n - 1; ++i) {
                right = std::max(right, dist3(pos, g[size_t(i)], g[size_t(i + 1)]));
            }
            if (cross > std::max(left, right) && cross > best) {
                best = cross;
                bestK = k;
            }
        }
    }
    if (bestK < 0) {
        return {g};
    }
    auto left = splitLong(pos, std::vector<uint32_t>(g.begin(), g.begin() + bestK));
    auto right = splitLong(pos, std::vector<uint32_t>(g.begin() + bestK, g.end()));
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void emitGroup(const std::vector<float>& pos, const std::vector<uint32_t>& g, std::vector<uint32_t>& faces)
{
    auto tri = [&](const std::vector<uint32_t>& p) {
        if (p.size() == 3) {
            faces.insert(faces.end(), {p[0], p[1], p[2]});
        } else if (p.size() == 4) {
            faces.insert(faces.end(), {p[0], p[1], p[2], p[1], p[3], p[2]});
        }
    };
    if (g.size() == 3 || g.size() == 4) {
        tri(g);
    } else if (g.size() > 4) {
        for (const auto& part : splitLong(pos, g)) {
            tri(part);
        }
    }
}

std::vector<uint32_t> stripFaces(const uint8_t* b, size_t n, size_t start, size_t length, uint32_t count,
                                 const std::vector<float>& pos)
{
    std::vector<uint32_t> faces;
    std::vector<uint32_t> cur;
    const size_t words = length / 2;
    for (size_t i = 0; i < words; ++i) {
        if (start + (i + 1) * 2 > n) {
            break;
        }
        const uint16_t x = be16(b + start + i * 2);
        if (x == 0xFFFF) {
            emitGroup(pos, cur, faces);
            cur.clear();
        } else if (x == 0) {
            if (!cur.empty()) {
                emitGroup(pos, cur, faces);
                cur.clear();
            } else {
                cur.push_back(0);
            }
        } else if (x < count) {
            cur.push_back(x);
        } else {
            emitGroup(pos, cur, faces);
            cur.clear();
            break;
        }
    }
    emitGroup(pos, cur, faces);
    return faces;
}

std::vector<TexRef> matRefs(const uint32_t* slot, const uint32_t* index, uint32_t nsam)
{
    std::vector<TexRef> refs;
    const uint32_t n = std::min(nsam, 8u);
    for (uint32_t k = 0; k < n; ++k) {
        if (index[k] == 0 && slot[k] == 0 && k > 0) {
            continue;
        }
        TexRef ref{slot[k], index[k]};
        bool seen = false;
        for (const TexRef& have : refs) {
            if (have.slot == ref.slot && have.index == ref.index) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            refs.push_back(ref);
        }
    }
    return refs;
}

std::string refToken(uint32_t slot, uint32_t index, const std::vector<std::string>* own,
                     const std::vector<std::string>* course)
{
    if (slot == 0) {
        return "an" + std::to_string(index);
    }
    if (slot == 1) {
        if (course && index < course->size() && !(*course)[index].empty()) {
            const std::string nm = cleanToken((*course)[index]);
            return nm.empty() ? "cs" + std::to_string(index) : nm;
        }
        return "cs" + std::to_string(index);
    }
    if (slot == 2 && own && index < own->size()) {
        if (!(*own)[index].empty()) {
            const std::string nm = cleanToken((*own)[index]);
            return nm.empty() ? "t" + std::to_string(index) : nm;
        }
        return "t" + std::to_string(index);
    }
    if (index == 0) {
        return "s" + std::to_string(slot);
    }
    return "s" + std::to_string(slot) + "_" + std::to_string(index);
}

std::string matName(const std::string& paint, int shapeI, int matI, uint32_t pair, const std::vector<TexRef>& refs,
                    const std::vector<std::string>* own, const std::vector<std::string>* course)
{
    std::string name;
    std::set<std::string> seen;
    for (const TexRef& ref : refs) {
        std::string token = refToken(ref.slot, ref.index, own, course);
        if (seen.insert(token).second) {
            if (!name.empty()) {
                name.push_back('_');
            }
            name += token;
        }
    }
    if (name.empty()) {
        name = "mtl" + std::to_string(pair);
    }
    if (matI == 0 && !paint.empty() && shapeI == 0) {
        name = paint + "_" + name;
    }
    return name;
}

void mirror(ShapeMesh& mesh)
{
    for (size_t i = 0; i < mesh.pos.size(); i += 3) {
        mesh.pos[i] = -mesh.pos[i];
    }
    for (size_t i = 0; i < mesh.nrm.size(); i += 3) {
        mesh.nrm[i] = -mesh.nrm[i];
    }
    for (Prim& prim : mesh.prims) {
        for (size_t i = 0; i + 2 < prim.indices.size(); i += 3) {
            std::swap(prim.indices[i + 1], prim.indices[i + 2]);
        }
    }
}

std::vector<std::pair<size_t, size_t>> listChunks(const uint8_t* b, size_t n, int entry)
{
    if (n < 8) {
        return {};
    }
    const uint32_t count = le32(b);
    if (count == 0 || count > 64 || 4 + size_t(count) * 4 > n) {
        return {};
    }
    if (entry < 0 || uint32_t(entry) >= count) {
        return {};
    }
    const uint32_t e = le32(b + 4 + size_t(entry) * 4);
    const uint32_t end = uint32_t(entry) + 1 < count ? le32(b + 4 + size_t(entry + 1) * 4) : uint32_t(n);
    if (e >= n || end > n || end < e || e + 4 > n) {
        return {};
    }
    const uint32_t cnt = le32(b + e);
    if (cnt == 0 || cnt >= 512 || e + 4 + size_t(cnt) * 4 > n) {
        return {};
    }
    std::vector<uint32_t> subs(cnt);
    for (uint32_t k = 0; k < cnt; ++k) {
        subs[k] = le32(b + e + 4 + size_t(k) * 4);
    }
    std::vector<std::pair<size_t, size_t>> out;
    for (uint32_t k = 0; k < cnt; ++k) {
        const size_t s = size_t(e) + subs[k];
        const size_t send = size_t(e) + (k + 1 < cnt ? subs[k + 1] : size_t(end) - e);
        if (s < send && send <= n) {
            out.emplace_back(s, send);
        }
    }
    return out;
}

const char* kLists[] = {"", "stand", "listA", "minimap", "listWhole", "routeB", "routeC", "points"};

} // namespace

bool isPartFile(const uint8_t* data, size_t n)
{
    return n >= kPart + 76 && be32(data + kPart) == kVersion;
}

bool dataFileSlice(const uint8_t* data, size_t n, int index, size_t& off, size_t& len)
{
    if (n < 8 || index < 0) {
        return false;
    }
    const uint32_t count = le32(data);
    if (count == 0 || count > 64 || uint32_t(index) >= count || 4 + size_t(count) * 4 > n) {
        return false;
    }
    off = le32(data + 4 + size_t(index) * 4);
    const size_t end = uint32_t(index) + 1 < count ? le32(data + 4 + size_t(index + 1) * 4) : n;
    if (off >= n || end > n || end <= off) {
        return false;
    }
    len = end - off;
    return true;
}

std::vector<std::string> ownXprNames(const uint8_t* data, size_t n)
{
    size_t off = 0, len = 0;
    if (!dataFileSlice(data, n, 1, off, len) || len < 16 || std::memcmp(data + off, "XPR2", 4) != 0) {
        return {};
    }
    try {
        return Xpr::parse(data + off, len, BcLayout::Pool).names();
    } catch (const std::exception&) {
        return {};
    }
}

std::vector<ShapeMesh> decodePart(const uint8_t* data, size_t n, const std::vector<std::string>* courseNames)
{
    if (!isPartFile(data, n)) {
        throw std::runtime_error("not a part file");
    }
    const uint32_t nShapes = be32(data + kPart + 12);
    const uint32_t shapeBase = be32(data + kPart + 28);
    if (nShapes > 64) {
        throw std::runtime_error("bad shape count");
    }
    std::string paint;
    const size_t hitFrom = std::min(n, size_t(0x200));
    const size_t hitTo = std::min(n, size_t(0x4000));
    for (size_t i = hitFrom; i + 4 < hitTo; ++i) {
        if (std::memcmp(data + i, "mat_", 4) == 0) {
            size_t end = i;
            while (end < n && data[end] > 32 && data[end] < 127) {
                ++end;
            }
            paint.assign(reinterpret_cast<const char*>(data + i), end - i);
            break;
        }
    }
    auto own = ownXprNames(data, n);
    const std::vector<std::string>* ownPtr = own.empty() ? nullptr : &own;

    std::vector<ShapeMesh> meshes;
    for (uint32_t si = 0; si < nShapes; ++si) {
        const size_t s = kPart + shapeBase + size_t(kShapeBytes) * si;
        if (s + kShapeBytes > n) {
            break;
        }
        struct Stream {
            uint32_t count, type, stride, size, off;
        };
        Stream streams[3];
        for (int k = 0; k < 3; ++k) {
            const size_t o = s + 240 + size_t(k) * 16;
            streams[k] = {be32(data + o), be32(data + o + 4), be32(data + o + 8), be32(data + o + 12),
                          be32(data + s + 384 + size_t(k) * 4)};
        }
        const Stream* byType[32] = {};
        for (const Stream& st : streams) {
            if (st.type < 32) {
                byType[st.type] = &st;
            }
        }
        if (!byType[11]) {
            throw std::runtime_error("no position stream");
        }
        const uint32_t count = byType[11]->count;
        if (count > 5000000) {
            throw std::runtime_error("vertex count");
        }
        ShapeMesh mesh;
        mesh.name = cstrAt(data, n, s, 24);
        mesh.pos.resize(size_t(count) * 3);
        const size_t pbase = kPart + byType[11]->off;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t o = pbase + size_t(i) * 12;
            if (o + 12 > n) {
                throw std::runtime_error("truncated positions");
            }
            mesh.pos[size_t(i) * 3] = beFloat(data + o);
            mesh.pos[size_t(i) * 3 + 1] = beFloat(data + o + 4);
            mesh.pos[size_t(i) * 3 + 2] = beFloat(data + o + 8);
        }
        auto readUv = [&](size_t o, std::vector<float>& dst) {
            if (o + 4 > n) {
                return;
            }
            dst.push_back(halfToFloat(be16(data + o)));
            dst.push_back(halfToFloat(be16(data + o + 2)));
        };
        if (byType[15] && byType[15]->stride == 12) {
            const size_t base = kPart + byType[15]->off;
            for (uint32_t i = 0; i < count; ++i) {
                const size_t o = base + size_t(i) * 12;
                if (o + 12 > n) {
                    break;
                }
                readUv(o + 4, mesh.uv);
                readUv(o + 8, mesh.uv2);
                mesh.col.push_back(data[o + 1] / 255.f);
                mesh.col.push_back(data[o + 2] / 255.f);
                mesh.col.push_back(data[o + 3] / 255.f);
            }
        } else if (byType[16] && byType[16]->stride == 16) {
            const size_t base = kPart + byType[16]->off;
            for (uint32_t i = 0; i < count; ++i) {
                const size_t o = base + size_t(i) * 16;
                if (o + 16 > n) {
                    break;
                }
                readUv(o + 4, mesh.uv);
                readUv(o + 8, mesh.uv2);
                readUv(o + 12, mesh.uv3);
                mesh.col.push_back(data[o + 1] / 255.f);
                mesh.col.push_back(data[o + 2] / 255.f);
                mesh.col.push_back(data[o + 3] / 255.f);
            }
        }
        if (byType[12] && byType[12]->stride == 8) {
            const size_t base = kPart + byType[12]->off;
            for (uint32_t i = 0; i < count; ++i) {
                const size_t o = base + size_t(i) * 8;
                if (o + 6 > n) {
                    break;
                }
                mesh.nrm.push_back(halfToFloat(be16(data + o)));
                mesh.nrm.push_back(halfToFloat(be16(data + o + 2)));
                mesh.nrm.push_back(halfToFloat(be16(data + o + 4)));
            }
        } else if (byType[13] && byType[13]->stride == 16) {
            const size_t base = kPart + byType[13]->off;
            for (uint32_t i = 0; i < count; ++i) {
                const size_t o = base + size_t(i) * 16;
                if (o + 6 > n) {
                    break;
                }
                mesh.nrm.push_back(halfToFloat(be16(data + o)));
                mesh.nrm.push_back(halfToFloat(be16(data + o + 2)));
                mesh.nrm.push_back(halfToFloat(be16(data + o + 4)));
            }
        }

        const uint32_t matOff = be32(data + s + 396);
        const size_t blk = kPart + matOff;
        if (blk + 16 > n) {
            meshes.push_back(std::move(mesh));
            continue;
        }
        const uint32_t n0 = be32(data + blk + 8);
        const uint32_t n1 = be32(data + blk + 12);
        const uint32_t nmat = n0 + n1;
        for (uint32_t j = 0; j < nmat && j < 4096; ++j) {
            const size_t e = blk + 16 + size_t(kMatBytes) * j;
            if (e + kMatBytes > n) {
                break;
            }
            const uint32_t nsam = be32(data + e + 128);
            uint32_t slots[8], indexes[8];
            for (int k = 0; k < 8; ++k) {
                slots[k] = be32(data + e + 144 + size_t(k) * 4);
                indexes[k] = be32(data + e + 176 + size_t(k) * 4);
            }
            const uint32_t stripLen = be32(data + e + 380);
            const uint32_t stripPtr = be32(data + e + 384);
            const uint32_t pair = be32(data + e + 392);
            const uint32_t ownPair = be32(data + e + 96);
            if (stripLen == 0 || kPart + size_t(stripPtr) + stripLen > n) {
                continue;
            }
            Prim prim;
            prim.refs = matRefs(slots, indexes, nsam);
            prim.indices = stripFaces(data, n, kPart + stripPtr, stripLen, count, mesh.pos);
            if (prim.indices.empty()) {
                continue;
            }
            prim.name = matName(paint, int(si), int(j), pair ? pair : ownPair, prim.refs, ownPtr, courseNames);
            mesh.prims.push_back(std::move(prim));
        }
        mirror(mesh);
        meshes.push_back(std::move(mesh));
    }
    return meshes;
}

CourseSet decodeCourseSet(const uint8_t* data, size_t n)
{
    CourseSet set;
    size_t off = 0, len = 0;
    if (dataFileSlice(data, n, 0, off, len) && len >= 16 && std::memcmp(data + off, "XPR2", 4) == 0) {
        try {
            set.textureNames = Xpr::parse(data + off, len, BcLayout::Pool).names();
            set.textureBlob.assign(data + off, data + off + len);
        } catch (const std::exception&) {
        }
    }
    const std::vector<std::string>* names = set.textureNames.empty() ? nullptr : &set.textureNames;
    for (int entry = 1; entry <= 7; ++entry) {
        auto chunks = listChunks(data, n, entry);
        int k = 0;
        for (const auto& range : chunks) {
            const uint8_t* part = data + range.first;
            const size_t pn = range.second - range.first;
            if (pn < 4 || pn > (1u << 30) || be32(part) != kVersion) {
                ++k;
                continue;
            }
            std::vector<uint8_t> wrapped(kPart + pn, 0);
            std::memcpy(wrapped.data() + kPart, part, pn);
            try {
                auto shapes = decodePart(wrapped.data(), wrapped.size(), names);
                size_t tris = 0;
                for (const auto& sh : shapes) {
                    for (const auto& prim : sh.prims) {
                        tris += prim.indices.size() / 3;
                    }
                }
                if (tris) {
                    CourseModel model;
                    char num[16];
                    std::snprintf(num, sizeof(num), "_m%02d", k);
                    model.name = std::string(kLists[entry]) + num;
                    model.shapes = std::move(shapes);
                    set.models.push_back(std::move(model));
                }
            } catch (const std::exception&) {
            }
            ++k;
        }
    }
    return set;
}

std::vector<Point> readCheckerPoints(const uint8_t* image, size_t n)
{
    constexpr uint32_t kVa = 0x82617750;
    constexpr uint32_t kBase = 0x82000000;
    constexpr int kGroups = 20;
    const size_t off = size_t(kVa - kBase);
    std::vector<Point> used;
    if (off + size_t(kGroups) * 256 > n) {
        return used;
    }
    for (int g = 0; g < kGroups; ++g) {
        for (int sub = 0; sub < 4; ++sub) {
            const uint8_t* w = image + off + size_t(g) * 256 + size_t(sub) * 64;
            for (int k = 0; k < 4; ++k) {
                Point p;
                p.x = beFloat(w + k * 16);
                p.y = beFloat(w + k * 16 + 4);
                p.z = beFloat(w + k * 16 + 8);
                p.heading = beFloat(w + k * 16 + 12);
                p.group = g;
                p.used = p.y != -10000000.f;
                if (p.used) {
                    used.push_back(p);
                }
            }
        }
    }
    return used;
}

std::vector<ShapeMesh> placeCheckers(const std::vector<ShapeMesh>& models, const std::vector<Point>& points)
{
    std::vector<ShapeMesh> inst;
    int i = 0;
    for (const Point& raw : points) {
        if (!raw.used) {
            continue;
        }
        const Point pt{-raw.x, raw.y, raw.z, raw.heading, true, raw.group};
        const float ang = pt.heading * 3.14159265f / 180.f;
        const float ca = std::cos(ang);
        const float sa = std::sin(ang);
        for (const ShapeMesh& sh : models) {
            ShapeMesh placed = sh;
            for (size_t v = 0; v < placed.pos.size(); v += 3) {
                const float x = placed.pos[v];
                const float z = placed.pos[v + 2];
                const float rx = x * ca - z * sa;
                const float rz = x * sa + z * ca;
                placed.pos[v] = -pt.x + -rx;
                placed.pos[v + 1] = pt.y + placed.pos[v + 1];
                placed.pos[v + 2] = pt.z + rz;
            }
            placed.name = models.size() > 1 ? "cs_point_" + std::to_string(i) + "_" + std::to_string(&sh - models.data())
                                             : "cs_point_" + std::to_string(i);
            inst.push_back(std::move(placed));
        }
        ++i;
    }
    return inst;
}

} // namespace txr
