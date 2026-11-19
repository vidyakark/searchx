#pragma once
// Variable-byte (varint) coding: 7 payload bits per byte, high bit = "more bytes follow".
// Small numbers (doc-id deltas, term frequencies, position deltas) take 1 byte instead of 4.
#include <cstdint>
#include <vector>

namespace searchx {

inline void vbEncode(uint32_t v, std::vector<uint8_t>& out) {
    while (v >= 128) {
        out.push_back(static_cast<uint8_t>((v & 127) | 128));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

inline uint32_t vbDecode(const uint8_t*& p) {
    uint32_t v = 0;
    int shift = 0;
    for (;;) {
        uint8_t b = *p++;
        v |= static_cast<uint32_t>(b & 127) << shift;
        if (!(b & 128)) break;
        shift += 7;
    }
    return v;
}

}  // namespace searchx
