#include "ucl.hpp"

#include <algorithm>
#include <stdexcept>

namespace txr {
namespace {

constexpr uint8_t kMagic[] = {0x00, 0xe9, 0x55, 0x43, 0x4c, 0xff, 0x01, 0x1a};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t n) : data_(data), n_(n) {}

    int bit()
    {
        if (remaining_ == 0) {
            if (pos_ + 4 > n_) {
                throw UclError("truncated NRV2B bitstream");
            }
            bits_ = uint32_t(data_[pos_]) | (uint32_t(data_[pos_ + 1]) << 8) |
                    (uint32_t(data_[pos_ + 2]) << 16) | (uint32_t(data_[pos_ + 3]) << 24);
            pos_ += 4;
            remaining_ = 32;
        }
        --remaining_;
        return (bits_ >> remaining_) & 1;
    }

    uint8_t byte()
    {
        if (pos_ >= n_) {
            throw UclError("truncated NRV2B byte stream");
        }
        return data_[pos_++];
    }

    size_t pos() const { return pos_; }

private:
    const uint8_t* data_;
    size_t n_;
    size_t pos_ = 0;
    uint32_t bits_ = 0;
    int remaining_ = 0;
};

uint32_t be32at(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

} // namespace

std::vector<uint8_t> decompressNrv2b(const uint8_t* data, size_t n, size_t expected)
{
    BitReader bits(data, n);
    std::vector<uint8_t> out;
    out.reserve(expected);
    uint32_t lastOffset = 1;
    while (true) {
        while (bits.bit()) {
            if (out.size() >= expected) {
                throw UclError("NRV2B output overrun");
            }
            out.push_back(bits.byte());
        }
        uint32_t offset = 1;
        while (true) {
            offset = offset * 2 + uint32_t(bits.bit());
            if (offset > 0xFFFFFFu + 3) {
                throw UclError("invalid NRV2B lookbehind");
            }
            if (bits.bit()) {
                break;
            }
        }
        if (offset == 2) {
            offset = lastOffset;
        } else {
            offset = (offset - 3) * 256 + bits.byte();
            if (offset == 0xFFFFFFFFu) {
                break;
            }
            offset += 1;
            lastOffset = offset;
        }
        uint32_t length = uint32_t(bits.bit());
        length = length * 2 + uint32_t(bits.bit());
        if (length == 0) {
            length = 1;
            while (true) {
                length = length * 2 + uint32_t(bits.bit());
                if (bits.bit()) {
                    break;
                }
            }
            length += 2;
        }
        length += offset > 0xD00;
        const uint32_t copySize = length + 1;
        if (offset > out.size()) {
            throw UclError("NRV2B lookbehind before start of output");
        }
        if (out.size() + copySize > expected) {
            throw UclError("NRV2B output overrun");
        }
        const size_t start = out.size() - offset;
        for (uint32_t i = 0; i < copySize; ++i) {
            out.push_back(out[start + (i % offset)]);
        }
    }
    if (bits.pos() != n) {
        throw UclError("NRV2B input not consumed");
    }
    if (out.size() != expected) {
        throw UclError("NRV2B size mismatch");
    }
    return out;
}

std::vector<uint8_t> decompressUclpack(const uint8_t* data, size_t n)
{
    size_t pos = 0;
    if (n >= 8 && std::equal(kMagic, kMagic + 8, data)) {
        pos = 8;
    }
    if (n < pos + 10) {
        throw UclError("truncated uclpack header");
    }
    const uint32_t flags = be32at(data + pos);
    const uint8_t method = data[pos + 4];
    const uint8_t level = data[pos + 5];
    const uint32_t blockSize = be32at(data + pos + 6);
    pos += 10;
    if (method != 0x2b) {
        throw UclError("unsupported UCL method");
    }
    if (level < 1 || level > 10) {
        throw UclError("invalid UCL level");
    }
    if (blockSize < 1 || blockSize > 64u * 1024u * 1024u) {
        throw UclError("invalid UCL block size");
    }
    std::vector<uint8_t> output;
    while (true) {
        if (pos + 4 > n) {
            throw UclError("missing uclpack end marker");
        }
        const uint32_t outputSize = be32at(data + pos);
        pos += 4;
        if (outputSize == 0) {
            break;
        }
        if (pos + 4 > n) {
            throw UclError("truncated uclpack block header");
        }
        const uint32_t inputSize = be32at(data + pos);
        pos += 4;
        if (inputSize == 0 || inputSize > outputSize || outputSize > blockSize || pos + inputSize > n) {
            throw UclError("invalid uclpack block sizes");
        }
        if (inputSize == outputSize) {
            output.insert(output.end(), data + pos, data + pos + inputSize);
        } else {
            auto block = decompressNrv2b(data + pos, inputSize, outputSize);
            output.insert(output.end(), block.begin(), block.end());
        }
        pos += inputSize;
    }
    if (flags & 1) {
        if (pos + 4 > n) {
            throw UclError("missing uclpack checksum");
        }
        pos += 4;
    }
    if (n - pos > 8) {
        throw UclError("trailing bytes after uclpack stream");
    }
    return output;
}

} // namespace txr
