#include "wmn.hpp"

#include "bytes.hpp"
#include "xpr.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <zlib.h>

namespace txr {
namespace {

std::vector<uint8_t> zlibInflate(const uint8_t* data, size_t n)
{
    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) {
        throw std::runtime_error("zlib init");
    }
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    strm.avail_in = uInt(n);
    std::vector<uint8_t> out;
    uint8_t buf[1 << 15];
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        strm.next_out = buf;
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            throw std::runtime_error("zlib chunk");
        }
        out.insert(out.end(), buf, buf + (sizeof(buf) - strm.avail_out));
        if (strm.avail_in == 0 && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            throw std::runtime_error("truncated zlib chunk");
        }
    }
    inflateEnd(&strm);
    return out;
}

struct Sub {
    uint16_t type = 0;
    uint16_t str = 0;
    uint32_t size = 0;
    uint32_t unk2 = 0;
    uint32_t unk3 = 0;
    size_t at = 0;
};

struct BitmapRef {
    uint32_t index = 0;
};

struct Material {
    std::string name;
    std::vector<BitmapRef> bitmaps;
};

void readStuff(const uint8_t* b, size_t n, size_t S, std::vector<ShapeMesh>& meshes, std::vector<std::string>& texNames)
{
    if (S + 0x94 > n) {
        return;
    }
    const uint32_t check = be32(b + S + 0x80);
    if (check != 0x00007042 && check != 0x42700000 && check != 0x0000F041 && check != 0x0000C041) {
        return;
    }
    const uint32_t count = be32(b + S + 0x88);
    if (count > 100000 || S + 0x90 + size_t(count) * 4 > n) {
        return;
    }
    std::vector<size_t> offs(count);
    for (uint32_t i = 0; i < count; ++i) {
        offs[i] = S + be32(b + S + 0x90 + size_t(i) * 4);
    }
    std::vector<Sub> subs;
    std::vector<std::string> strings;
    subs.reserve(count);
    for (size_t o : offs) {
        if (o + 16 > n) {
            subs.push_back({});
            continue;
        }
        Sub sub;
        sub.type = be16(b + o);
        sub.str = be16(b + o + 2);
        sub.size = be32(b + o + 4);
        sub.unk2 = be32(b + o + 8);
        sub.unk3 = be32(b + o + 12);
        sub.at = o;
        subs.push_back(sub);
        if (sub.type == 0x15 && o + 16 < n) {
            strings.push_back(cstrAt(b, n, o + 16, 128));
        }
    }
    auto strAt = [&](uint16_t index) -> std::string {
        if (index == 0 || index > strings.size()) {
            return {};
        }
        return strings[index - 1];
    };
    std::vector<Material> materials;
    for (const Sub& sub : subs) {
        if (sub.type == 0x0C) {
            texNames.push_back(strAt(sub.str));
        } else if (sub.type == 0x07 && sub.at + 32 <= n) {
            Material mat;
            mat.name = strAt(sub.str);
            const size_t body = sub.at + 16;
            const uint32_t nsub = be32(b + body + 12);
            size_t p = body + 16;
            for (uint32_t i = 0; i < nsub && i < 64; ++i) {
                if (p + 16 > n) {
                    break;
                }
                const uint32_t type = be32(b + p);
                const uint32_t size = be32(b + p + 4);
                const uint32_t unk1 = be32(b + p + 8);
                if (size < 16 || p + size > n) {
                    break;
                }
                if (type == 0x00 || type == 0x08) {
                    mat.bitmaps.push_back({unk1});
                }
                p += size;
            }
            materials.push_back(std::move(mat));
        }
    }
    for (const Sub& sub : subs) {
        if (sub.type != 0x08 || sub.at + 16 + 64 > n) {
            continue;
        }
        size_t p = sub.at + 16;
        p += 16; // 4 u32
        p += 32; // two vec4
        if (p + 32 > n) {
            continue;
        }
        p += 8;
        p += 4; // c1
        const uint32_t c2 = be32(b + p);
        p += 4;
        p += 16;
        ShapeMesh mesh;
        mesh.name = strAt(sub.str);
        if (mesh.name.empty()) {
            mesh.name = "blk" + std::to_string(sub.str);
        }
        std::vector<uint32_t> faces;
        bool flip = true;
        for (uint32_t block = 0; block < c2 && block < 64; ++block) {
            if (p + 32 > n) {
                break;
            }
            const size_t gp = p;
            const uint32_t blockSize = be32(b + p + 4);
            const uint32_t comp = be32(b + p + 8);
            const uint32_t cnt = be32(b + p + 20);
            p += 32;
            if (comp == 0x02) {
                for (uint32_t i = 0; i < cnt; ++i) {
                    if (p + 16 > n) {
                        break;
                    }
                    const float x = beFloat(b + p);
                    const float y = beFloat(b + p + 4);
                    const float z = beFloat(b + p + 8);
                    mesh.pos.insert(mesh.pos.end(), {x, -z, y});
                    p += 16;
                }
            } else if (comp == 0x05) {
                for (uint32_t i = 0; i < cnt; ++i) {
                    if (p + 8 > n) {
                        break;
                    }
                    mesh.uv.push_back(beFloat(b + p));
                    mesh.uv.push_back(1.f - beFloat(b + p + 4));
                    p += 8;
                }
            } else if (comp == 0x00) {
                bool reset = true;
                uint32_t x = 0;
                uint32_t a = 0, bidx = 0, c = 0;
                while (x < cnt) {
                    x += 1;
                    if (reset) {
                        x += 2;
                        reset = false;
                        if (p + 6 > n) {
                            break;
                        }
                        a = be16(b + p);
                        bidx = be16(b + p + 2);
                        c = be16(b + p + 4);
                        p += 6;
                        if (flip) {
                            faces.insert(faces.end(), {a, c, bidx});
                            flip = false;
                        } else {
                            faces.insert(faces.end(), {a, bidx, c});
                            flip = true;
                        }
                    } else {
                        a = bidx;
                        bidx = c;
                        if (p + 2 > n) {
                            break;
                        }
                        c = be16(b + p);
                        p += 2;
                        if (c < 0xFFFF) {
                            if (flip) {
                                faces.insert(faces.end(), {a, c, bidx});
                                flip = false;
                            } else {
                                faces.insert(faces.end(), {a, bidx, c});
                                flip = true;
                            }
                        } else {
                            reset = true;
                        }
                    }
                }
            }
            if (blockSize >= 32 && gp + blockSize <= n) {
                p = gp + blockSize;
            }
        }
        if (!mesh.pos.empty() && !faces.empty()) {
            Prim prim;
            prim.indices = std::move(faces);
            const uint32_t matid = sub.unk3;
            if (matid < materials.size()) {
                prim.name = materials[matid].name.empty() ? mesh.name : materials[matid].name;
                if (!materials[matid].bitmaps.empty()) {
                    const uint32_t tx = materials[matid].bitmaps[0].index;
                    if (tx < texNames.size()) {
                        prim.name = texNames[tx].empty() ? prim.name : texNames[tx];
                    }
                }
            } else {
                prim.name = mesh.name;
            }
            mesh.prims.push_back(std::move(prim));
            meshes.push_back(std::move(mesh));
        }
    }
}

void readXtd(const uint8_t* b, size_t n, size_t base, const std::vector<std::string>& texNames,
             std::vector<std::pair<std::string, Image>>& images)
{
    if (base + 12 > n) {
        return;
    }
    const uint32_t count = le32(b + base + 8);
    if (count == 0 || count > 100000 || base + 12 + size_t(count) * 4 > n) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = base + le32(b + base + 12 + size_t(i) * 4);
        if (at + 16 > n) {
            continue;
        }
        const uint32_t size = le32(b + at);
        if (size <= 0x10 || at + size > n || size < 48) {
            continue;
        }
        auto gtf = decodeGtf(b + at + 16, size - 16);
        if (gtf.empty() || gtf[0].rgba.empty()) {
            continue;
        }
        std::string name = i < texNames.size() && !texNames[i].empty() ? texNames[i] : "embedded_" + std::to_string(i);
        Image image;
        image.width = gtf[0].width;
        image.height = gtf[0].height;
        image.rgba = std::move(gtf[0].rgba);
        images.emplace_back(std::move(name), std::move(image));
    }
}

void walkXmd(const uint8_t* b, size_t n, size_t start, std::vector<ShapeMesh>& meshes,
             std::vector<std::string>& texNames, std::vector<size_t>& xtd, int depth)
{
    if (depth > 8 || start + 16 > n) {
        return;
    }
    const uint32_t count = be32(b + start + 12);
    if (count > 100000 || start + 16 + size_t(count) * 8 > n) {
        return;
    }
    std::vector<size_t> offs(count);
    for (uint32_t i = 0; i < count; ++i) {
        offs[i] = start + be32(b + start + 16 + size_t(i) * 4);
    }
    for (size_t o : offs) {
        if (o + 4 > n) {
            continue;
        }
        const uint32_t ft = be32(b + o);
        if (ft == 0) {
            readStuff(b, n, o, meshes, texNames);
        } else if (ft == 0x01444D58 || ft == 0x584D4401) {
            walkXmd(b, n, o, meshes, texNames, xtd, depth + 1);
        } else if (ft == 0x00445458 || ft == 0x58544400) {
            xtd.push_back(o);
        }
    }
}

} // namespace

std::vector<WmnEntry> parseWmnToc(const uint8_t* toc, size_t n)
{
    if (n < 0x30 || std::memcmp(toc, "BLDh", 4) != 0) {
        throw std::runtime_error("not a WMN TOC");
    }
    const uint32_t count = le32(toc + 0x14);
    const uint32_t alignment = le32(toc + 0x20);
    if (count > 1000000 || alignment == 0 || 0x30 + size_t(count) * 40 > n) {
        throw std::runtime_error("bad WMN TOC");
    }
    std::vector<std::string> names;
    const uint8_t* tbl = nullptr;
    for (size_t i = 0; i + 4 < n; ++i) {
        if (std::memcmp(toc + i, "tbl ", 4) == 0) {
            tbl = toc + i;
            break;
        }
    }
    if (tbl && size_t(tbl - toc) + 8 < n) {
        size_t pos = size_t(tbl - toc) + 8;
        for (uint32_t i = 0; i < count && pos < n; ++i) {
            const size_t end = std::find(toc + pos, toc + n, 0) - toc;
            names.emplace_back(reinterpret_cast<const char*>(toc + pos), end - pos);
            pos = end + 1;
        }
    }
    std::vector<WmnEntry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = toc + 0x30 + size_t(i) * 40;
        WmnEntry ent;
        ent.offset = uint64_t(le32(e)) * alignment;
        ent.stored = le32(e + 4);
        ent.size = le32(e + 8);
        ent.name = i < names.size() && !names[i].empty() ? names[i] : "entry_" + std::to_string(i);
        entries.push_back(std::move(ent));
    }
    return entries;
}

std::vector<uint8_t> inflateWmn(const uint8_t* dat, size_t n, size_t off, size_t fileSize)
{
    if (off + 60 > n) {
        throw std::runtime_error("WMN chunk past end");
    }
    uint32_t sz = le32(dat + off + 52);
    size_t pos = off + 60;
    std::vector<uint8_t> out;
    int guard = 0;
    while (out.size() < fileSize && guard++ < 20000) {
        if (sz == 0 || pos + sz > n) {
            throw std::runtime_error("bad WMN chunk");
        }
        auto piece = zlibInflate(dat + pos, sz);
        out.insert(out.end(), piece.begin(), piece.end());
        pos += sz;
        if (out.size() >= fileSize) {
            break;
        }
        pos = (pos + 3) & ~size_t(3);
        if (pos + 8 > n) {
            break;
        }
        sz = le32(dat + pos);
        pos += 8;
        if (sz == 0 || sz > 0x400000) {
            break;
        }
    }
    if (out.size() < fileSize) {
        throw std::runtime_error("WMN inflate short");
    }
    out.resize(fileSize);
    return out;
}

WmnDecoded decodeGrid(const uint8_t* data, size_t n)
{
    WmnDecoded decoded;
    if (n >= 4 && (std::memcmp(data, "XMD", 3) == 0 || be32(data) == 0x01444D58)) {
        std::vector<std::string> names;
        std::vector<size_t> xtd;
        walkXmd(data, n, 0, decoded.meshes, names, xtd, 0);
        for (size_t at : xtd) {
            readXtd(data, n, at, names, decoded.images);
        }
    }
    return decoded;
}

} // namespace txr
