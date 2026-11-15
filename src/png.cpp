#include "png.hpp"

#include <zlib.h>

#include <cstring>

namespace txr {
namespace {

uint32_t be32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
}

void chunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t n)
{
    const uint32_t len = be32(uint32_t(n));
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 4);
    const size_t mark = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data, data + n);
    const uLong crc = crc32(0, out.data() + mark, uInt(out.size() - mark));
    const uint32_t c = be32(uint32_t(crc));
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&c), reinterpret_cast<const uint8_t*>(&c) + 4);
}

} // namespace

std::vector<uint8_t> encodePng(const Image& image)
{
    const int w = image.width;
    const int h = image.height;
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const uint8_t* row = image.rgba.data() + size_t(y) * size_t(w) * 4;
        raw.insert(raw.end(), row, row + size_t(w) * 4);
    }
    uLongf bound = compressBound(uLong(raw.size()));
    std::vector<uint8_t> z(bound);
    if (compress2(z.data(), &bound, raw.data(), uLong(raw.size()), 6) != Z_OK) {
        return {};
    }
    z.resize(bound);

    uint8_t ihdr[13];
    const uint32_t wb = be32(uint32_t(w));
    const uint32_t hb = be32(uint32_t(h));
    std::memcpy(ihdr, &wb, 4);
    std::memcpy(ihdr + 4, &hb, 4);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    std::vector<uint8_t> png;
    static const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig, sig + 8);
    chunk(png, "IHDR", ihdr, 13);
    chunk(png, "IDAT", z.data(), z.size());
    chunk(png, "IEND", nullptr, 0);
    return png;
}

bool mostlyTransparent(const Image& image)
{
    if (image.rgba.empty()) {
        return false;
    }
    size_t clear = 0;
    const size_t pixels = image.rgba.size() / 4;
    for (size_t i = 0; i < pixels; ++i) {
        if (image.rgba[i * 4 + 3] < 250) {
            ++clear;
        }
    }
    return pixels > 0 && clear * 100 >= pixels * 35;
}

} // namespace txr
