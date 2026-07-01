//
// Lenient DLS loading: repair of malformed RIFF chunk sizes.
//
// FluidSynth's DLS parser is strict about RIFF chunk sizes. Some exporters
// (notably Awave Studio) write incorrect sizes that make it read a phantom chunk
// past the real data and bail out with "early EOF" — even though lenient players
// (Fruity LSD, etc.) load the same file fine. FluidSynth's DLS loader reads the
// file directly (it does NOT go through the sfloader file callbacks), so we can't
// fix it in memory; instead FluidSynthModel repairs a copy to a temp file and
// loads that. This header holds the pure repair routine so the standalone test
// harness (tools/font_qa.cpp) exercises the exact code the plugin ships.
//

#pragma once

#include <cstdint>
#include <cstring>
#include <cstddef>

namespace juicysf {

// Repair a "DLS " RIFF image in place. Returns true if anything was changed
// (i.e. the buffer was a malformed DLS). No-op for non-DLS or well-formed data.
inline bool repairDlsImage(uint8_t* d, size_t n) {
    if (n < 12) return false;
    if (std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "DLS ", 4) != 0) return false;

    auto rd32 = [&](size_t o) -> uint32_t {
        return (uint32_t) d[o] | ((uint32_t) d[o+1] << 8)
             | ((uint32_t) d[o+2] << 16) | ((uint32_t) d[o+3] << 24);
    };
    auto wr32 = [&](size_t o, uint32_t v) {
        d[o]   = (uint8_t) (v & 0xff);         d[o+1] = (uint8_t) ((v >> 8)  & 0xff);
        d[o+2] = (uint8_t) ((v >> 16) & 0xff); d[o+3] = (uint8_t) ((v >> 24) & 0xff);
    };

    bool changed = false;

    // Walk top-level chunks. The first one that runs past EOF is a phantom chunk
    // created by an undersized preceding chunk (the Awave bug): grow the previous
    // real chunk so it absorbs the remainder up to EOF.
    size_t pos = 12;
    long long prevSizeOff = -1;
    size_t prevBody = 0;
    while (pos + 8 <= n) {
        const uint32_t size = rd32(pos + 4);
        const size_t body = pos + 8;
        const size_t end = body + size;
        if (end > n) {
            const uint32_t want = (uint32_t) (n - prevBody);
            if (prevSizeOff >= 0 && rd32((size_t) prevSizeOff) != want) {
                wr32((size_t) prevSizeOff, want);
                changed = true;
            }
            break;
        }
        prevSizeOff = (long long) (pos + 4);
        prevBody = body;
        pos = end + (end & 1); // RIFF chunks are word-aligned
    }

    // Never let the outer RIFF claim more bytes than the file actually has.
    if ((size_t) rd32(4) + 8 > n) {
        wr32(4, (uint32_t) (n - 8));
        changed = true;
    }

    return changed;
}

} // namespace juicysf
