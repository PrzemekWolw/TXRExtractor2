#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace txr {

inline uint16_t rd16(const uint8_t* p, bool be)
{
    return be ? uint16_t((p[0] << 8) | p[1]) : uint16_t(p[0] | (p[1] << 8));
}

inline uint32_t rd32(const uint8_t* p, bool be)
{
    if (be) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint16_t be16(const uint8_t* p) { return rd16(p, true); }
inline uint32_t be32(const uint8_t* p) { return rd32(p, true); }
inline uint16_t le16(const uint8_t* p) { return rd16(p, false); }
inline uint32_t le32(const uint8_t* p) { return rd32(p, false); }

inline float beFloat(const uint8_t* p)
{
    const uint32_t u = be32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

inline float halfToFloat(uint16_t h)
{
    const uint32_t sign = uint32_t(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t out;
    if (exp == 0) {
        if (mant == 0) {
            out = sign;
        } else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400) == 0) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3ff;
            out = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out = sign | 0x7f800000u | (mant << 13);
    } else {
        out = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &out, 4);
    return f;
}

struct View {
    const uint8_t* p = nullptr;
    size_t n = 0;

    bool has(size_t o, size_t k) const { return o <= n && k <= n - o; }
    uint32_t u32(size_t o, bool be) const
    {
        if (!has(o, 4)) {
            throw std::runtime_error("truncated");
        }
        return rd32(p + o, be);
    }
    uint16_t u16(size_t o, bool be) const
    {
        if (!has(o, 2)) {
            throw std::runtime_error("truncated");
        }
        return rd16(p + o, be);
    }
    float f32(size_t o) const
    {
        if (!has(o, 4)) {
            throw std::runtime_error("truncated");
        }
        return beFloat(p + o);
    }
};

inline std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n < 0) {
        throw std::runtime_error("cannot size " + path.string());
    }
    in.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    if (n > 0 && !in.read(reinterpret_cast<char*>(buf.data()), n)) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return buf;
}

inline std::vector<uint8_t> readSlice(std::istream& in, uint64_t off, size_t n)
{
    in.clear();
    in.seekg(static_cast<std::streamoff>(off));
    if (!in) {
        throw std::runtime_error("seek failed");
    }
    std::vector<uint8_t> buf(n);
    if (n > 0 && !in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n))) {
        throw std::runtime_error("short read");
    }
    return buf;
}

inline void writeFile(const std::filesystem::path& path, const uint8_t* p, size_t n)
{
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out || (n > 0 && !out.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n)))) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

inline void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& buf)
{
    writeFile(path, buf.data(), buf.size());
}

inline std::string cstrAt(const uint8_t* p, size_t n, size_t o, size_t cap = 64)
{
    if (o >= n) {
        return {};
    }
    size_t end = o;
    const size_t lim = std::min(n, o + cap);
    while (end < lim && p[end] != 0) {
        ++end;
    }
    return std::string(reinterpret_cast<const char*>(p + o), end - o);
}

inline std::string cleanToken(const std::string& nm, size_t cap = 24)
{
    std::string out;
    out.reserve(std::min(nm.size(), cap));
    for (unsigned char c : nm) {
        if (out.size() >= cap) {
            break;
        }
        if (std::isalnum(c) || c == '_' || c == '-') {
            out.push_back(char(c));
        } else {
            out.push_back('_');
        }
    }
    return out;
}

} // namespace txr
