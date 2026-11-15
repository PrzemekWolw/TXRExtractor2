#include "xpr.hpp"

#include "bytes.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace txr {
namespace {

int log2Ceil(int v)
{
    if (v <= 1) {
        return 0;
    }
    int n = 0;
    int x = v - 1;
    while (x > 0) {
        x >>= 1;
        ++n;
    }
    return n;
}

int log2Pow2(int v)
{
    int n = 0;
    while ((1 << n) < v) {
        ++n;
    }
    return n;
}

int alignUp(int v, int a) { return (v + a - 1) / a * a; }
int divUp(int v, int d) { return (v + d - 1) / d; }
int nextPow2(int v) { return 1 << log2Ceil(v); }

struct Layout {
    int width = 0;
    int height = 0;
    int tiled = 0;
    int pitchTexels = 0;
    int base = 0;
    int mip = 0;
    int minLevel = 0;
    int maxLevel = 0;
    int packedLevel = 0x7fffffff;
};

bool getLayout(const uint32_t* d, Layout& l)
{
    if (((d[5] >> 9) & 3) != 1) {
        return false;
    }
    l.width = int(d[2] & 0x1fff) + 1;
    l.height = int((d[2] >> 13) & 0x1fff) + 1;
    l.tiled = int((d[0] >> 31) & 1);
    l.pitchTexels = int((d[0] >> 22) & 0x1ff) << 5;
    const int sizeMax = log2Ceil(std::max(l.width, l.height));
    int basePage = int((d[1] >> 12) & 0xfffff);
    int mipPage = int((d[5] >> 12) & 0xfffff);
    int minLevel = 0;
    int maxLevel = 0;
    if (mipPage) {
        minLevel = std::min(int((d[4] >> 2) & 0xf), sizeMax);
        maxLevel = std::max(std::min(int((d[4] >> 6) & 0xf), sizeMax), minLevel);
        if (minLevel) {
            basePage = 0;
        }
    } else {
        mipPage = 0;
    }
    if (!basePage && !mipPage) {
        l.base = 0;
        l.mip = 0;
        l.minLevel = 0;
        l.maxLevel = 0;
        l.packedLevel = 0x7fffffff;
        return true;
    }
    l.base = basePage << 12;
    l.mip = mipPage << 12;
    l.minLevel = minLevel;
    l.maxLevel = maxLevel;
    l.packedLevel = 0x7fffffff;
    if ((d[5] >> 11) & 1) {
        const int log2Size = log2Ceil(std::min(l.width, l.height));
        l.packedLevel = log2Size > 4 ? log2Size - 4 : 0;
    }
    return true;
}

struct Place {
    int address = 0;
    int x = 0;
    int y = 0;
    int pitchBlocks = 0;
    int rowPitch = 0;
    int layerStride = 0;
};

void strides(const Layout& l, int bw, int bh, int bpb, int stored, bool base, Place& p)
{
    const int pitchTexels = base ? l.pitchTexels : std::max(nextPow2(l.width) >> stored, 1);
    const int rowsTexels = base ? l.height : std::max(nextPow2(l.height) >> stored, 1);
    p.pitchBlocks = alignUp(divUp(pitchTexels, bw), 32);
    p.rowPitch = p.pitchBlocks * bpb;
    if (!l.tiled && !base) {
        p.rowPitch = alignUp(p.rowPitch, 256);
    }
    const int rows = alignUp(divUp(rowsTexels, bh), 32);
    p.layerStride = alignUp(p.rowPitch * rows, 4096);
}

void packedMipOffset(const Layout& l, int bw, int bh, int level, int& x, int& y)
{
    const int log2w = log2Ceil(l.width);
    const int log2h = log2Ceil(l.height);
    const int log2Size = std::min(log2w, log2h);
    const int packedBase = log2Size > 4 ? log2Size - 4 : 0;
    const int packed = level - packedBase;
    const bool wide = log2w > log2h;
    x = 0;
    y = 0;
    if (packed < 3) {
        const int v = 16 >> packed;
        if (wide) {
            y = v;
        } else {
            x = v;
        }
    } else {
        const int offset = (1 << (std::max(log2w, log2h) - packedBase)) >> (packed - 2);
        if (wide) {
            x = offset;
        } else {
            y = offset;
        }
    }
    x /= bw;
    y /= bh;
}

Place placeLevel(const Layout& l, int bw, int bh, int bpb, int level)
{
    Place p;
    const int stored = std::min(level, l.packedLevel);
    if (level == 0) {
        p.address = l.base;
        strides(l, bw, bh, bpb, 0, true, p);
    } else {
        p.address = l.mip;
        for (int s = 1; s < stored; ++s) {
            strides(l, bw, bh, bpb, s, false, p);
            p.address += p.layerStride;
        }
        strides(l, bw, bh, bpb, stored, false, p);
    }
    if (level >= l.packedLevel) {
        packedMipOffset(l, bw, bh, level, p.x, p.y);
    }
    return p;
}

int tiledOffset2D(int x, int y, int pitch, int bpbLog2)
{
    pitch = alignUp(pitch, 32);
    const int macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpbLog2 + 7);
    const int micro = ((x & 7) + ((y & 0xE) << 2)) << bpbLog2;
    const int offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F));
}

void copyBytes(const uint8_t* blob, size_t n, int addr, int need, uint8_t* dst)
{
    if (addr < 0 || need < 0) {
        std::memset(dst, 0, size_t(std::max(need, 0)));
        return;
    }
    const size_t a = size_t(addr);
    if (a >= n) {
        std::memset(dst, 0, size_t(need));
        return;
    }
    const size_t have = std::min(size_t(need), n - a);
    std::memcpy(dst, blob + a, have);
    if (have < size_t(need)) {
        std::memset(dst + have, 0, size_t(need) - have);
    }
}

void swapEndian(std::vector<uint8_t>& buf, int endian)
{
    if (endian == 1) {
        for (size_t i = 0; i + 1 < buf.size(); i += 2) {
            std::swap(buf[i], buf[i + 1]);
        }
    } else if (endian == 2) {
        for (size_t i = 0; i + 3 < buf.size(); i += 4) {
            std::swap(buf[i], buf[i + 3]);
            std::swap(buf[i + 1], buf[i + 2]);
        }
    } else if (endian == 3) {
        for (size_t i = 0; i + 3 < buf.size(); i += 4) {
            std::swap(buf[i], buf[i + 2]);
            std::swap(buf[i + 1], buf[i + 3]);
        }
    }
}

std::vector<uint8_t> gather(const uint8_t* blob, size_t n, const Layout& l, int bw, int bh, int bpb, const Place& p,
                            int level, int& wb, int& hb)
{
    const int lw = std::max(l.width >> level, 1);
    const int lh = std::max(l.height >> level, 1);
    wb = divUp(lw, bw);
    hb = divUp(lh, bh);
    std::vector<uint8_t> out(size_t(wb) * size_t(hb) * size_t(bpb));
    if (!l.tiled) {
        for (int y = 0; y < hb; ++y) {
            const int addr = p.address + (y + p.y) * p.rowPitch + p.x * bpb;
            copyBytes(blob, n, addr, wb * bpb, out.data() + size_t(y) * size_t(wb) * size_t(bpb));
        }
        return out;
    }
    const int bpbLog2 = log2Pow2(bpb);
    const int run = bpbLog2 ? (1 << (4 - std::min(bpbLog2, 4))) : 8;
    for (int y = 0; y < hb; ++y) {
        int x = p.x;
        const int end = x + wb;
        uint8_t* row = out.data() + size_t(y) * size_t(wb) * size_t(bpb);
        int filled = 0;
        while (x < end) {
            const int addr = p.address + tiledOffset2D(x, y + p.y, p.pitchBlocks, bpbLog2);
            const int count = std::min(run - (x & (run - 1)), end - x);
            const int need = count << bpbLog2;
            copyBytes(blob, n, addr, need, row + filled);
            filled += need;
            x += count;
        }
    }
    return out;
}

void expand565(uint32_t v, uint8_t rgb[3])
{
    rgb[0] = uint8_t((((v >> 11) & 31) * 255) / 31);
    rgb[1] = uint8_t((((v >> 5) & 63) * 255) / 63);
    rgb[2] = uint8_t(((v & 31) * 255) / 31);
}

// Block-major RGBA, 16 pixels per block, row-major inside the 4x4.
void colorBlock(const uint8_t* lin, int nb, int bpb, int ibOff, int cbOff, bool punch, std::vector<uint8_t>& px)
{
    px.assign(size_t(nb) * 16 * 4, 0);
    for (int i = 0; i < nb; ++i) {
        const uint8_t* blk = lin + size_t(i) * size_t(bpb);
        const uint8_t* ib = blk + ibOff;
        const uint8_t* cb = blk + cbOff;
        const uint32_t c0 = uint32_t(cb[0]) | (uint32_t(cb[1]) << 8);
        const uint32_t c1 = uint32_t(cb[2]) | (uint32_t(cb[3]) << 8);
        uint8_t rgb[4][3];
        expand565(c0, rgb[0]);
        expand565(c1, rgb[1]);
        const bool four = punch ? c0 > c1 : true;
        uint8_t alpha[4] = {255, 255, 255, 255};
        if (four) {
            for (int c = 0; c < 3; ++c) {
                rgb[2][c] = uint8_t((2 * rgb[0][c] + rgb[1][c]) / 3);
                rgb[3][c] = uint8_t((rgb[0][c] + 2 * rgb[1][c]) / 3);
            }
        } else {
            for (int c = 0; c < 3; ++c) {
                rgb[2][c] = uint8_t((rgb[0][c] + rgb[1][c]) / 2);
                rgb[3][c] = 0;
            }
            alpha[3] = 0;
        }
        const uint32_t bits = uint32_t(ib[0]) | (uint32_t(ib[1]) << 8) | (uint32_t(ib[2]) << 16) | (uint32_t(ib[3]) << 24);
        for (int t = 0; t < 16; ++t) {
            const int idx = int((bits >> (2 * t)) & 3);
            uint8_t* o = px.data() + (size_t(i) * 16 + size_t(t)) * 4;
            o[0] = rgb[idx][0];
            o[1] = rgb[idx][1];
            o[2] = rgb[idx][2];
            o[3] = alpha[idx];
        }
    }
}

void placeBlocks(const std::vector<uint8_t>& px, int hb, int wb, Image& image)
{
    image.width = wb * 4;
    image.height = hb * 4;
    image.rgba.assign(size_t(image.width) * size_t(image.height) * 4, 0);
    for (int by = 0; by < hb; ++by) {
        for (int bx = 0; bx < wb; ++bx) {
            const uint8_t* block = px.data() + (size_t(by) * size_t(wb) + size_t(bx)) * 16 * 4;
            for (int py = 0; py < 4; ++py) {
                for (int pxel = 0; pxel < 4; ++pxel) {
                    const int y = by * 4 + py;
                    const int x = bx * 4 + pxel;
                    std::memcpy(image.rgba.data() + (size_t(y) * size_t(image.width) + size_t(x)) * 4,
                                block + (size_t(py) * 4 + size_t(pxel)) * 4, 4);
                }
            }
        }
    }
}

void applySwizzle(Image& image, uint32_t swizzle)
{
    if (swizzle == 0x688) {
        return;
    }
    std::vector<uint8_t> out(image.rgba.size());
    const size_t pixels = image.rgba.size() / 4;
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t* s = image.rgba.data() + i * 4;
        uint8_t* d = out.data() + i * 4;
        for (int c = 0; c < 4; ++c) {
            const uint32_t sel = (swizzle >> (3 * c)) & 7;
            if (sel == 4) {
                d[c] = 0;
            } else if (sel >= 5) {
                d[c] = 255;
            } else {
                d[c] = s[std::min<uint32_t>(sel, 3)];
            }
        }
    }
    image.rgba.swap(out);
}

const char* fmtName(int format, int& bw, int& bh, int& bpb)
{
    switch (format) {
    case 18: bw = bh = 4; bpb = 8; return "DXT1";
    case 19: bw = bh = 4; bpb = 16; return "DXT23";
    case 20: bw = bh = 4; bpb = 16; return "DXT45";
    case 6: bw = bh = 1; bpb = 4; return "8888";
    case 15: bw = bh = 1; bpb = 2; return "4444";
    default: return nullptr;
    }
}

Image crop(Image image, int lw, int lh)
{
    if (image.width == lw && image.height == lh) {
        return image;
    }
    Image out;
    out.width = lw;
    out.height = lh;
    out.rgba.assign(size_t(lw) * size_t(lh) * 4, 0);
    const int cw = std::min(image.width, lw);
    const int ch = std::min(image.height, lh);
    for (int y = 0; y < ch; ++y) {
        std::memcpy(out.rgba.data() + size_t(y) * size_t(lw) * 4,
                    image.rgba.data() + size_t(y) * size_t(image.width) * 4, size_t(cw) * 4);
    }
    return out;
}

} // namespace

Image decodeBlocks(const uint8_t* lin, size_t n, int wb, int hb, const char* fmt, int bpb, BcLayout layout)
{
    const int nb = wb * hb;
    if (nb <= 0 || size_t(nb) * size_t(bpb) > n) {
        return {};
    }
    const bool xenos = layout == BcLayout::Xenos;
    Image image;
    if (std::strcmp(fmt, "DXT1") == 0 || std::strcmp(fmt, "DXT1np") == 0) {
        const int ib = xenos ? 0 : 4;
        const int cb = xenos ? 4 : 0;
        std::vector<uint8_t> px;
        colorBlock(lin, nb, bpb, ib, cb, std::strcmp(fmt, "DXT1") == 0, px);
        placeBlocks(px, hb, wb, image);
        return image;
    }
    if (std::strcmp(fmt, "DXT23") == 0 || std::strcmp(fmt, "DXT45") == 0) {
        std::vector<uint8_t> color(size_t(nb) * 8);
        for (int i = 0; i < nb; ++i) {
            std::memcpy(color.data() + size_t(i) * 8, lin + size_t(i) * 16 + 8, 8);
        }
        Image colorImg = decodeBlocks(color.data(), color.size(), wb, hb, "DXT1np", 8, layout);
        image = std::move(colorImg);
        for (int i = 0; i < nb; ++i) {
            const uint8_t* blk = lin + size_t(i) * 16;
            uint8_t alpha[16];
            if (std::strcmp(fmt, "DXT23") == 0) {
                for (int t = 0; t < 16; ++t) {
                    const int shift = (t & 1) * 4;
                    alpha[t] = uint8_t((((blk[t / 2] >> shift) & 0xf) * 17));
                }
            } else {
                const int a0 = blk[0];
                const int a1 = blk[1];
                int pal[8];
                pal[0] = a0;
                pal[1] = a1;
                if (a0 > a1) {
                    for (int k = 1; k <= 6; ++k) {
                        pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
                    }
                } else {
                    for (int k = 1; k <= 4; ++k) {
                        pal[k + 1] = ((5 - k) * a0 + k * a1) / 5;
                    }
                    pal[6] = 0;
                    pal[7] = 255;
                }
                uint64_t bits = 0;
                for (int b = 0; b < 6; ++b) {
                    bits |= uint64_t(blk[2 + b]) << (8 * b);
                }
                for (int t = 0; t < 16; ++t) {
                    alpha[t] = uint8_t(pal[(bits >> (3 * t)) & 7]);
                }
            }
            const int by = i / wb;
            const int bx = i % wb;
            for (int t = 0; t < 16; ++t) {
                const int y = by * 4 + t / 4;
                const int x = bx * 4 + t % 4;
                if (y < image.height && x < image.width) {
                    image.rgba[(size_t(y) * size_t(image.width) + size_t(x)) * 4 + 3] = alpha[t];
                }
            }
        }
        return image;
    }
    if (std::strcmp(fmt, "8888") == 0) {
        image.width = wb;
        image.height = hb;
        image.rgba.assign(lin, lin + size_t(nb) * 4);
        return image;
    }
    if (std::strcmp(fmt, "4444") == 0) {
        image.width = wb;
        image.height = hb;
        image.rgba.resize(size_t(nb) * 4);
        for (int i = 0; i < nb; ++i) {
            const uint32_t v = uint32_t(lin[i * 2]) | (uint32_t(lin[i * 2 + 1]) << 8);
            for (int c = 0; c < 4; ++c) {
                image.rgba[size_t(i) * 4 + size_t(c)] = uint8_t(((v >> (4 * c)) & 0xf) * 17);
            }
        }
        return image;
    }
    return {};
}

Xpr Xpr::parse(const uint8_t* blob, size_t n, BcLayout layout)
{
    Xpr x;
    x.layout_ = layout;
    if (n < 16 || std::memcmp(blob, "XPR2", 4) != 0) {
        throw std::runtime_error("not an XPR2");
    }
    const uint32_t dataOff = be32(blob + 4);
    const uint32_t pix = be32(blob + 8);
    const uint32_t count = be32(blob + 12);
    if (count > 8192 || 16 + size_t(count) * 16 > n) {
        throw std::runtime_error("bad XPR2 count");
    }
    if (layout == BcLayout::Pool) {
        // Same synth the course/own exporters use: data_off grows by 12 and the
        // bytes after the 16-byte header stay the TX2D table.
        x.blob_.resize(16 + (n - 16));
        std::memcpy(x.blob_.data(), "XPR2", 4);
        const uint32_t shifted = dataOff + 12;
        x.blob_[4] = uint8_t(shifted >> 24);
        x.blob_[5] = uint8_t(shifted >> 16);
        x.blob_[6] = uint8_t(shifted >> 8);
        x.blob_[7] = uint8_t(shifted);
        std::memcpy(x.blob_.data() + 8, blob + 8, 8);
        if (n > 16) {
            std::memcpy(x.blob_.data() + 16, blob + 16, n - 16);
        }
        x.dataOff_ = shifted;
    } else {
        x.blob_.assign(blob, blob + n);
        x.dataOff_ = dataOff;
    }
    x.pix_ = pix;
    const uint8_t* region = blob + 12;
    const size_t regionN = n - 12;
    static const uint8_t marker[] = {0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00};
    x.names_.resize(count);
    x.ents_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t ent = 4 + size_t(i) * 16;
        if (ent + 16 > regionN) {
            break;
        }
        const uint32_t nameOff = be32(region + ent + 12);
        x.names_[i] = cstrAt(region, regionN, nameOff, 64);
        const uint32_t detail = be32(region + ent + 4);
        const uint8_t* search = x.blob_.data();
        const size_t searchN = x.blob_.size();
        const size_t from = std::min(size_t(detail), searchN);
        const size_t to = std::min(from + 0x40, searchN);
        const uint8_t* hit = std::search(search + from, search + to, marker, marker + 8);
        if (hit == search + to) {
            continue;
        }
        const size_t fo = size_t(hit - search) + 8;
        if (fo + 24 > searchN) {
            continue;
        }
        for (int w = 0; w < 6; ++w) {
            x.ents_[i].d[w] = be32(search + fo + size_t(w) * 4);
        }
        x.ents_[i].fetch = true;
        if (x.names_[i].empty()) {
            x.names_[i] = cstrAt(search, searchN, detail, 16);
        }
    }
    return x;
}

Image Xpr::image(int index) const
{
    if (index < 0 || index >= count() || !ents_[size_t(index)].fetch) {
        return {};
    }
    const uint32_t* d = ents_[size_t(index)].d;
    Layout layout;
    if (!getLayout(d, layout)) {
        return {};
    }
    int bw = 0, bh = 0, bpb = 0;
    const char* fmt = fmtName(int(d[1] & 0x3f), bw, bh, bpb);
    if (!fmt) {
        return {};
    }
    layout.base += int(dataOff_);
    layout.mip += int(dataOff_);
    const int endian = int((d[1] >> 6) & 3);
    int wb = 0, hb = 0;
    Place p = placeLevel(layout, bw, bh, bpb, 0);
    auto lin = gather(blob_.data(), blob_.size(), layout, bw, bh, bpb, p, 0, wb, hb);
    swapEndian(lin, endian);
    Image image = decodeBlocks(lin.data(), lin.size(), wb, hb, fmt, bpb, layout_);
    const int lw = std::max(layout.width, 1);
    const int lh = std::max(layout.height, 1);
    image = crop(std::move(image), lw, lh);
    applySwizzle(image, (d[3] >> 1) & 0xfff);
    return image;
}

std::vector<GtfImage> decodeGtf(const uint8_t* data, size_t n)
{
    std::vector<GtfImage> images;
    size_t pos = 0;
    for (int guard = 0; guard < 64 && pos + 32 <= n; ++guard) {
        if (le32(data + pos) != 0x00000501) {
            break;
        }
        const size_t base = pos;
        const uint32_t dataAddr = be32(data + base + 16);
        const uint32_t dataSize = be32(data + base + 20);
        const uint8_t fmt = data[base + 24];
        const int w = int(be16(data + base + 32));
        const int h = int(be16(data + base + 34));
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
            break;
        }
        const char* name = nullptr;
        int bw = 1, bh = 1, bpb = 4;
        bool raw = false;
        if (fmt == 0x86) {
            name = "DXT1";
            bw = bh = 4;
            bpb = 8;
        } else if (fmt == 0x87 || fmt == 0xa7) {
            name = "DXT23";
            bw = bh = 4;
            bpb = 16;
        } else if (fmt == 0x88) {
            name = "DXT45";
            bw = bh = 4;
            bpb = 16;
        } else if (fmt == 0x85) {
            raw = true;
            bpb = 4;
        } else {
            break;
        }
        const int wb = divUp(w, bw);
        const int hb = divUp(h, bh);
        const size_t need = raw ? size_t(w) * size_t(h) * 4 : size_t(wb) * size_t(hb) * size_t(bpb);
        const size_t src = base + dataAddr;
        if (src > n) {
            break;
        }
        std::vector<uint8_t> lin(need, 0);
        const size_t have = std::min(need, n - src);
        std::memcpy(lin.data(), data + src, have);
        GtfImage image;
        image.width = w;
        image.height = h;
        if (raw) {
            image.rgba.resize(need);
            for (size_t i = 0; i < need; i += 4) {
                image.rgba[i] = lin[i + 2];
                image.rgba[i + 1] = lin[i + 1];
                image.rgba[i + 2] = lin[i];
                image.rgba[i + 3] = lin[i + 3];
            }
        } else {
            Image decoded = decodeBlocks(lin.data(), lin.size(), wb, hb, name, bpb, BcLayout::Pool);
            decoded = crop(std::move(decoded), w, h);
            image.rgba = std::move(decoded.rgba);
        }
        images.push_back(std::move(image));
        const size_t next = base + dataAddr + dataSize;
        if (next <= pos || next + 4 > n || le32(data + next) != 0x00000501) {
            break;
        }
        pos = next;
    }
    return images;
}

} // namespace txr
