#pragma once

#include "png.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace txr {

enum class BcLayout { Xenos, Pool };

// One 4x4-block grid of already-linear, endian-swapped BC or raw pixels.
Image decodeBlocks(const uint8_t* lin, size_t n, int wb, int hb, const char* fmt, int bpb, BcLayout layout);

class Xpr {
public:
    static Xpr parse(const uint8_t* blob, size_t n, BcLayout layout);

    int count() const { return int(names_.size()); }
    const std::string& name(int i) const { return names_[size_t(i)]; }
    const std::vector<std::string>& names() const { return names_; }
    // Base level. Empty image when the entry is not a supported 2D texture.
    Image image(int index) const;

private:
    BcLayout layout_ = BcLayout::Xenos;
    std::vector<uint8_t> blob_;
    uint32_t dataOff_ = 0;
    uint32_t pix_ = 0;
    struct Ent {
        uint32_t d[6]{};
        bool fetch = false;
    };
    std::vector<Ent> ents_;
    std::vector<std::string> names_;
};

struct GtfImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// One image per 0x00000501 header dumpGFT understands. Further images are
// decoded only when another header follows the first image's payload.
std::vector<GtfImage> decodeGtf(const uint8_t* data, size_t n);

} // namespace txr
