#include "selftest.hpp"

#include "bytes.hpp"
#include "glb.hpp"
#include "itc_mesh.hpp"
#include "png.hpp"
#include "ucl.hpp"
#include "wmn.hpp"
#include "xpr.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <zlib.h>

namespace fs = std::filesystem;

namespace {

int fails = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::cerr << "FAIL " << what << "\n";
        ++fails;
    }
}

void putBe32(std::vector<uint8_t>& b, size_t o, uint32_t v)
{
    b[o] = uint8_t(v >> 24);
    b[o + 1] = uint8_t(v >> 16);
    b[o + 2] = uint8_t(v >> 8);
    b[o + 3] = uint8_t(v);
}

void putBe16(std::vector<uint8_t>& b, size_t o, uint16_t v)
{
    b[o] = uint8_t(v >> 8);
    b[o + 1] = uint8_t(v);
}

void putLe32(std::vector<uint8_t>& b, size_t o, uint32_t v)
{
    b[o] = uint8_t(v);
    b[o + 1] = uint8_t(v >> 8);
    b[o + 2] = uint8_t(v >> 16);
    b[o + 3] = uint8_t(v >> 24);
}

std::vector<uint8_t> fromHex(const char* hex)
{
    std::vector<uint8_t> out;
    for (const char* p = hex; p[0] && p[1]; p += 2) {
        auto nibble = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        out.push_back(uint8_t((nibble(p[0]) << 4) | nibble(p[1])));
    }
    return out;
}

bool white(const txr::Image& image)
{
    if (image.rgba.size() != 4 * 16) {
        return false;
    }
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
        if (image.rgba[i] != 255 || image.rgba[i + 1] != 255 || image.rgba[i + 2] != 255) {
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> oneTrianglePart()
{
    std::vector<uint8_t> part(0x830, 0);
    putBe32(part, 0x20, 0x00150000);
    putBe32(part, 0x2C, 1);
    putBe32(part, 0x3C, 0x50);
    std::memcpy(part.data() + 0x70, "mdl_Detail", 10);
    const size_t s = 0x70;
    putBe32(part, s + 240, 3);
    putBe32(part, s + 244, 11);
    putBe32(part, s + 248, 12);
    putBe32(part, s + 252, 36);
    putBe32(part, s + 256, 3);
    putBe32(part, s + 260, 12);
    putBe32(part, s + 264, 8);
    putBe32(part, s + 268, 24);
    putBe32(part, s + 272, 3);
    putBe32(part, s + 276, 15);
    putBe32(part, s + 280, 12);
    putBe32(part, s + 284, 36);
    putBe32(part, s + 384, 0x400);
    putBe32(part, s + 388, 0x430);
    putBe32(part, s + 392, 0x450);
    putBe32(part, s + 396, 0x500);
    putBe32(part, 0x528, 1);
    const size_t e = 0x530;
    putBe32(part, e + 128, 1);
    putBe32(part, e + 144, 2);
    putBe32(part, e + 176, 3);
    putBe32(part, e + 380, 8);
    putBe32(part, e + 384, 0x800);
    const float xyz[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (int i = 0; i < 9; ++i) {
        uint32_t u;
        std::memcpy(&u, &xyz[i], 4);
        putBe32(part, 0x420 + size_t(i) * 4, u);
    }
    for (int i = 0; i < 3; ++i) {
        putBe16(part, 0x450 + size_t(i) * 8 + 2, 0x3c00);
    }
    for (int i = 0; i < 3; ++i) {
        const size_t o = 0x470 + size_t(i) * 12;
        part[o + 1] = 0xff;
        part[o + 2] = 0x01;
        part[o + 3] = 0x02;
        putBe16(part, o + 4, 0x3800);
        putBe16(part, o + 6, 0x3400);
        putBe16(part, o + 10, 0x3c00);
    }
    putBe16(part, 0x820, 0);
    putBe16(part, 0x822, 1);
    putBe16(part, 0x824, 2);
    putBe16(part, 0x826, 0xffff);
    return part;
}

void testCodecs()
{
    const auto nrv = fromHex("000000c0486900200100ff");
    auto hi = txr::decompressNrv2b(nrv.data(), nrv.size(), 2);
    expect(hi.size() == 2 && hi[0] == 'H' && hi[1] == 'i', "nrv2b Hi");

    const auto stored = fromHex("000000002b070000040000000004000000047465737400000000");
    auto test = txr::decompressUclpack(stored.data(), stored.size());
    expect(test.size() == 4 && std::memcmp(test.data(), "test", 4) == 0, "uclpack stored");

    const uint8_t dxt[8] = {0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    expect(white(txr::decodeBlocks(dxt, 8, 1, 1, "DXT1", 8, txr::BcLayout::Pool)), "dxt1 pool white");
    expect(!white(txr::decodeBlocks(dxt, 8, 1, 1, "DXT1", 8, txr::BcLayout::Xenos)), "dxt1 xenos differs");

    std::vector<uint8_t> gtf(0x80 + 8, 0);
    gtf[0] = 0x01;
    gtf[1] = 0x05;
    putBe32(gtf, 8, 1);
    putBe32(gtf, 16, 0x80);
    putBe32(gtf, 20, 8);
    gtf[24] = 0x86;
    putBe16(gtf, 32, 4);
    putBe16(gtf, 34, 4);
    std::memcpy(gtf.data() + 0x80, dxt, 8);
    auto images = txr::decodeGtf(gtf.data(), gtf.size());
    expect(images.size() == 1 && images[0].width == 4 && images[0].height == 4, "gtf size");
    txr::Image gtfImage{4, 4, images[0].rgba};
    expect(white(gtfImage), "gtf white");

    const uint8_t payload[] = {'H', 'i'};
    uLongf bound = compressBound(sizeof(payload));
    std::vector<uint8_t> z(bound);
    compress2(z.data(), &bound, payload, sizeof(payload), 6);
    std::vector<uint8_t> garc(60 + bound, 0);
    putLe32(garc, 52, uint32_t(bound));
    std::memcpy(garc.data() + 60, z.data(), bound);
    auto inflated = txr::inflateWmn(garc.data(), garc.size(), 0, 2);
    expect(inflated.size() == 2 && inflated[0] == 'H' && inflated[1] == 'i', "garc inflate");
}

std::vector<uint8_t> oneTriangleGrid()
{
    auto be32 = [](std::vector<uint8_t>& b, uint32_t v) {
        b.push_back(uint8_t(v >> 24));
        b.push_back(uint8_t(v >> 16));
        b.push_back(uint8_t(v >> 8));
        b.push_back(uint8_t(v));
    };
    auto be16 = [](std::vector<uint8_t>& b, uint16_t v) {
        b.push_back(uint8_t(v >> 8));
        b.push_back(uint8_t(v));
    };
    auto bef = [&](std::vector<uint8_t>& b, float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        be32(b, u);
    };
    std::vector<uint8_t> geo;
    for (int i = 0; i < 4; ++i) {
        be32(geo, 0);
    }
    for (float v : {0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f}) {
        bef(geo, v);
    }
    be32(geo, 0);
    be32(geo, 0);
    be32(geo, 0);
    be32(geo, 2);
    for (int i = 0; i < 4; ++i) {
        be32(geo, 0);
    }
    auto attr = [&](uint32_t comp, const std::vector<uint8_t>& payload, uint32_t count) {
        be32(geo, 0x000A0000);
        be32(geo, 32 + uint32_t(payload.size()));
        be32(geo, comp);
        be32(geo, 0);
        be32(geo, 0);
        be32(geo, count);
        be32(geo, 0);
        be32(geo, 0);
        geo.insert(geo.end(), payload.begin(), payload.end());
    };
    std::vector<uint8_t> pos;
    for (float v : {1.f, 2.f, 3.f, 1.f, 4.f, 5.f, 6.f, 1.f, 7.f, 8.f, 9.f, 1.f}) {
        bef(pos, v);
    }
    std::vector<uint8_t> faces;
    be16(faces, 0);
    be16(faces, 1);
    be16(faces, 2);
    attr(0x02, pos, 3);
    attr(0x00, faces, 3);

    std::vector<uint8_t> stuff(0xA0 + 16 + geo.size(), 0);
    stuff[0x80] = 0x42;
    stuff[0x81] = 0x70;
    stuff[0x8b] = 1;
    stuff[0x93] = 0xA0;
    stuff[0xA0] = 0x00;
    stuff[0xA1] = 0x08;
    std::memcpy(stuff.data() + 0xB0, geo.data(), geo.size());

    std::vector<uint8_t> file(0x20, 0);
    file[0] = 'X';
    file[1] = 'M';
    file[2] = 'D';
    file[3] = 1;
    file[15] = 1;
    file[18] = 0x00;
    file[19] = 0x20;
    const uint32_t size = uint32_t(stuff.size());
    file[20] = uint8_t(size >> 24);
    file[21] = uint8_t(size >> 16);
    file[22] = uint8_t(size >> 8);
    file[23] = uint8_t(size);
    file.insert(file.end(), stuff.begin(), stuff.end());
    return file;
}
void testMeshes()
{
    auto grid = oneTriangleGrid();
    auto decoded = txr::decodeGrid(grid.data(), grid.size());
    expect(decoded.meshes.size() == 1, "xmd mesh count");
    if (!decoded.meshes.empty() && !decoded.meshes[0].prims.empty()) {
        const auto& mesh = decoded.meshes[0];
        const float want[] = {1, -3, 2, 4, -6, 5, 7, -9, 8};
        bool pos = mesh.pos.size() == 9;
        for (int i = 0; pos && i < 9; ++i) {
            pos = std::fabs(mesh.pos[size_t(i)] - want[i]) < 1e-4f;
        }
        const auto& idx = mesh.prims[0].indices;
        expect(pos, "xmd positions");
        expect(idx.size() == 3 && idx[0] == 0 && idx[1] == 2 && idx[2] == 1, "xmd face");
    } else {
        expect(false, "xmd face");
    }

    auto part = oneTrianglePart();
    auto meshes = txr::decodePart(part.data(), part.size(), nullptr);
    expect(meshes.size() == 1 && !meshes[0].prims.empty(), "part mesh");
    if (!meshes.empty() && !meshes[0].prims.empty()) {
        expect(std::fabs(meshes[0].pos[0] + 1) < 1e-4f && std::fabs(meshes[0].pos[1] - 2) < 1e-4f &&
                   std::fabs(meshes[0].pos[2] - 3) < 1e-4f,
               "part mirrored position");
        const auto& idx = meshes[0].prims[0].indices;
        expect(idx.size() == 3 && idx[0] == 0 && idx[1] == 2 && idx[2] == 1, "part face");
        expect(meshes[0].prims[0].name == "s2_3", "part material");
        expect(meshes[0].uv.size() >= 2 && std::fabs(meshes[0].uv[0] - 0.5f) < 1e-3f &&
                   std::fabs(meshes[0].uv[1] - 0.25f) < 1e-3f,
               "part uv");
    }

    const std::vector<uint8_t> bare(part.begin() + 0x20, part.end());
    std::vector<uint8_t> fileA(16 + 8 + bare.size(), 0);
    putLe32(fileA, 0, 2);
    putLe32(fileA, 4, 12);
    putLe32(fileA, 8, 16);
    putLe32(fileA, 16, 1);
    putLe32(fileA, 20, 8);
    std::memcpy(fileA.data() + 24, bare.data(), bare.size());
    auto course = txr::decodeCourseSet(fileA.data(), fileA.size());
    expect(course.models.size() == 1 && course.models[0].name == "stand_m00", "fileA prelude");

    auto glb = txr::buildGlb(meshes);
    expect(glb.size() > 20 && std::memcmp(glb.data(), "glTF", 4) == 0, "glb header");
}

struct GlbCounts {
    int vertices = 0;
    int indices = 0;
    int prims = 0;
};

GlbCounts readGlb(const fs::path& path)
{
    auto bytes = txr::readFile(path);
    GlbCounts counts;
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "glTF", 4) != 0) {
        return counts;
    }
    const uint32_t jsonLen = txr::le32(bytes.data() + 12);
    if (20 + jsonLen > bytes.size()) {
        return counts;
    }
    auto json = nlohmann::json::parse(bytes.begin() + 20, bytes.begin() + 20 + jsonLen);
    if (!json.contains("meshes") || !json.contains("accessors")) {
        return counts;
    }
    for (const auto& mesh : json["meshes"]) {
        int meshVerts = 0;
        for (const auto& prim : mesh["primitives"]) {
            ++counts.prims;
            if (prim.contains("attributes") && prim["attributes"].contains("POSITION")) {
                meshVerts = std::max(meshVerts, json["accessors"][prim["attributes"]["POSITION"].get<int>()]["count"].get<int>());
            }
            if (prim.contains("indices")) {
                counts.indices += json["accessors"][prim["indices"].get<int>()]["count"].get<int>();
            }
        }
        counts.vertices += meshVerts;
    }
    return counts;
}

int meshCounts(const std::vector<txr::ShapeMesh>& meshes)
{
    int n = 0;
    for (const auto& mesh : meshes) {
        n += int(mesh.pos.size() / 3);
    }
    return n;
}

int indexCounts(const std::vector<txr::ShapeMesh>& meshes)
{
    int n = 0;
    for (const auto& mesh : meshes) {
        for (const auto& prim : mesh.prims) {
            n += int(prim.indices.size());
        }
    }
    return n;
}

txr::Image readPng(const fs::path& path)
{
    auto bytes = txr::readFile(path);
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) != 0) {
        throw std::runtime_error("not a png");
    }
    int width = 0, height = 0, color = 0;
    std::vector<uint8_t> idat;
    size_t pos = 8;
    while (pos + 12 <= bytes.size()) {
        const uint32_t len = txr::be32(bytes.data() + pos);
        const char* type = reinterpret_cast<const char*>(bytes.data() + pos + 4);
        const uint8_t* data = bytes.data() + pos + 8;
        if (pos + 12 + len > bytes.size()) {
            break;
        }
        if (std::memcmp(type, "IHDR", 4) == 0) {
            width = int(txr::be32(data));
            height = int(txr::be32(data + 4));
            color = data[9];
            if (data[8] != 8 || data[10] != 0) {
                throw std::runtime_error("unsupported png");
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }
    const int ch = color == 6 ? 4 : color == 2 ? 3 : 0;
    if (ch == 0 || width <= 0 || height <= 0) {
        throw std::runtime_error("unsupported png");
    }
    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) {
        throw std::runtime_error("png inflate");
    }
    strm.next_in = idat.data();
    strm.avail_in = uInt(idat.size());
    std::vector<uint8_t> raw;
    uint8_t buf[1 << 15];
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        strm.next_out = buf;
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            throw std::runtime_error("png inflate");
        }
        raw.insert(raw.end(), buf, buf + (sizeof(buf) - strm.avail_out));
    }
    inflateEnd(&strm);
    const int stride = width * ch;
    txr::Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(size_t(width) * size_t(height) * 4);
    size_t src = 0;
    std::vector<uint8_t> prev(size_t(stride), 0);
    std::vector<uint8_t> row(size_t(stride), 0);
    auto paeth = [](int a, int b, int c) {
        const int p = a + b - c;
        const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
        return (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
    };
    for (int y = 0; y < height; ++y) {
        if (src >= raw.size()) {
            throw std::runtime_error("truncated png");
        }
        const int filter = raw[src++];
        if (src + size_t(stride) > raw.size()) {
            throw std::runtime_error("truncated png");
        }
        for (int i = 0; i < stride; ++i) {
            const int x = raw[src++];
            const int a = i >= ch ? row[size_t(i - ch)] : 0;
            const int b = prev[size_t(i)];
            const int c = i >= ch ? prev[size_t(i - ch)] : 0;
            int v = x;
            if (filter == 1) v += a;
            else if (filter == 2) v += b;
            else if (filter == 3) v += (a + b) / 2;
            else if (filter == 4) v += paeth(a, b, c);
            row[size_t(i)] = uint8_t(v);
        }
        for (int x = 0; x < width; ++x) {
            uint8_t* dst = image.rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;
            const uint8_t* px = row.data() + size_t(x) * size_t(ch);
            dst[0] = px[0];
            dst[1] = px[1];
            dst[2] = px[2];
            dst[3] = ch == 4 ? px[3] : 255;
        }
        prev = row;
    }
    return image;
}

void localChecks()
{
    const fs::path root = "/mnt/Data_SSD/Portable/xenia";
    const fs::path gtfs[] = {
        root / "wmn_extract/COURSE/MAP_MASK.GTF",
        root / "wmn_extract/CAR/HEADLIGHTMASK/HEADLIGHTMASK.GTF",
        root / "wmn_extract/CAR/ENVMAP/CARENV001.GTF",
    };
    const int expectW[] = {264, 0, 512};
    for (int i = 0; i < 3; ++i) {
        if (!fs::exists(gtfs[i])) {
            std::cout << "skip " << gtfs[i].filename().string() << "\n";
            continue;
        }
        auto bytes = txr::readFile(gtfs[i]);
        auto images = txr::decodeGtf(bytes.data(), bytes.size());
        expect(!images.empty() && !images[0].rgba.empty(), gtfs[i].filename().string().c_str());
        if (!images.empty()) {
            std::cout << gtfs[i].filename().string() << " " << images[0].width << "x" << images[0].height << "\n";
            if (expectW[i]) {
                expect(images[0].width == expectW[i] && images[0].height == expectW[i], "gtf dimension");
            }
            const fs::path png = fs::temp_directory_path() / (gtfs[i].stem().string() + "_txr2.png");
            txr::Image image{images[0].width, images[0].height, images[0].rgba};
            txr::writeFile(png, txr::encodePng(image));
        }
    }
    const fs::path rec = root / "itc_files/rec_0002624.bin";
    const fs::path glb = root / "itc_decoded/meshm/rec_0002624.glb";
    if (fs::exists(rec) && fs::exists(glb)) {
        auto bytes = txr::readFile(rec);
        if (!txr::isPartFile(bytes.data(), bytes.size())) {
            bytes = txr::decompressUclpack(bytes.data(), bytes.size());
        }
        auto meshes = txr::decodePart(bytes.data(), bytes.size(), nullptr);
        const GlbCounts have = readGlb(glb);
        expect(meshCounts(meshes) == have.vertices, "record 2624 vertices");
        expect(indexCounts(meshes) == have.indices, "record 2624 indices");
        std::cout << "rec 2624 verts " << meshCounts(meshes) << " idx " << indexCounts(meshes) << " glb " << have.vertices
                  << "/" << have.indices << "\n";
    }
    const fs::path fileA = root / "itc_course_fileA.bin";
    const fs::path stand = root / "itc_decoded/courseset/stand_m00.glb";
    const fs::path coursePng = root / "itc_decoded/course/0000_4go_ro_kakiwari.png";
    if (fs::exists(fileA) && fs::exists(stand)) {
        auto bytes = txr::readFile(fileA);
        auto set = txr::decodeCourseSet(bytes.data(), bytes.size());
        const txr::CourseModel* model = nullptr;
        int standCount = 0;
        for (const auto& item : set.models) {
            if (item.name.rfind("stand_", 0) == 0) {
                ++standCount;
                if (item.name == "stand_m00") {
                    model = &item;
                }
            }
        }
        int files = 0;
        for (const auto& entry : fs::directory_iterator(stand.parent_path())) {
            if (entry.path().filename().string().rfind("stand_", 0) == 0) {
                ++files;
            }
        }
        expect(standCount == files, "fileA stand count");
        if (model) {
            const GlbCounts have = readGlb(stand);
            expect(meshCounts(model->shapes) == have.vertices, "stand vertices");
            expect(indexCounts(model->shapes) == have.indices, "stand indices");
        }
        if (fs::exists(coursePng) && !set.textureBlob.empty()) {
            txr::Xpr xpr = txr::Xpr::parse(set.textureBlob.data(), set.textureBlob.size(), txr::BcLayout::Pool);
            txr::Image got = xpr.image(0);
            txr::Image want = readPng(coursePng);
            bool same = got.width == want.width && got.height == want.height && got.rgba == want.rgba;
            expect(same && xpr.name(0) == "4go_ro_kakiwari", "course texture 0");
            std::cout << "course0 " << xpr.name(0) << " " << got.width << "x" << got.height << (same ? " match" : " differ") << "\n";
        }
    }
    const fs::path grid = root / "wmn_extract/COURSE/SHUTOKO/NIGHT/AREA_11GO_BUIL_CUT_11GO_DAI_000.GRID";
    if (fs::exists(grid)) {
        auto bytes = txr::readFile(grid);
        auto decoded = txr::decodeGrid(bytes.data(), bytes.size());
        int verts = 0, faces = 0;
        for (const auto& mesh : decoded.meshes) {
            verts += int(mesh.pos.size() / 3);
            for (const auto& prim : mesh.prims) {
                faces += int(prim.indices.size() / 3);
            }
        }
        expect(decoded.meshes.size() == 17 && verts == 13172 && faces == 17634, "wmn grid counts");
        if (!decoded.meshes.empty() && !decoded.meshes[0].prims.empty() && decoded.meshes[0].pos.size() >= 3) {
            const auto& idx = decoded.meshes[0].prims[0].indices;
            expect(idx.size() >= 3 && idx[0] == 0 && idx[1] == 2 && idx[2] == 1, "wmn grid winding");
            expect(std::fabs(decoded.meshes[0].pos[0] - 3424.0415f) < 0.01f, "wmn grid position");
        }
        std::cout << "grid " << decoded.meshes.size() << " meshes " << verts << " verts " << faces << " tris\n";
    }
}

} // namespace

int selftest()
{
    try {
        testCodecs();
        testMeshes();
        localChecks();
    } catch (const std::exception& ex) {
        std::cerr << "FAIL exception " << ex.what() << "\n";
        return 1;
    }
    if (fails) {
        std::cerr << fails << " failed\n";
        return 1;
    }
    std::cout << "selftest ok\n";
    return 0;
}
