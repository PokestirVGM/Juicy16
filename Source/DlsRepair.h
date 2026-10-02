// Repairs malformed DLS RIFF chunk sizes.
//
// Some exporters (notably Awave Studio) write sizes that make FluidSynth's strict
// parser read a phantom chunk and fail with "early EOF"; lenient players load the
// file. The DLS loader bypasses sfloader file callbacks, so FluidSynthModel
// repairs a temp copy instead. Kept header-only so tools/font_qa.cpp tests the
// shipped routine.

#pragma once

#include <cstdint>
#include <cstring>
#include <cstddef>

namespace juicysf {

enum class DlsRepairScan { notNeeded, needed, readFailed };

namespace detail {

inline uint32_t dlsRead32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8)
        | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

// The same decisions drive both the read-only stream preflight and in-memory
// repair. Readers copy exactly `count` bytes at `offset`, returning false on a
// short read; writers receive only the size fields that actually need changing.
template <typename Reader, typename Writer>
DlsRepairScan scanDlsRepairs(size_t n, Reader& read, Writer& write) {
    if (n < 12)
        return DlsRepairScan::notNeeded;
    uint8_t header[12]{};
    if (!read(0, header, sizeof(header)))
        return DlsRepairScan::readFailed;
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "DLS ", 4) != 0)
        return DlsRepairScan::notNeeded;

    bool changed{false};
    size_t pos{12}, previousSizeOffset{0}, previousBody{0};
    uint32_t previousSize{0};
    bool hasPrevious{false};
    while (pos <= n && n - pos >= 8) {
        uint8_t sizeBytes[4]{};
        if (!read(pos + 4, sizeBytes, sizeof(sizeBytes)))
            return DlsRepairScan::readFailed;
        const uint32_t size{dlsRead32(sizeBytes)};
        const size_t body{pos + 8};
        // Subtract before comparing so even hostile sizes cannot overflow.
        if (static_cast<size_t>(size) > n - body) {
            const uint32_t wanted{static_cast<uint32_t>(n - previousBody)};
            if (hasPrevious && previousSize != wanted) {
                write(previousSizeOffset, wanted);
                changed = true;
            }
            break;
        }
        hasPrevious = true;
        previousSizeOffset = pos + 4;
        previousBody = body;
        previousSize = size;
        const size_t end{body + size};
        // An odd chunk ending at EOF has no complete successor, so no padding
        // byte needs reading (and adding it must not overflow a maximum size).
        if (end == n)
            break;
        pos = end + (end & 1);
    }
    if (static_cast<size_t>(dlsRead32(header + 4)) > n - 8) {
        write(4, static_cast<uint32_t>(n - 8));
        changed = true;
    }
    return changed ? DlsRepairScan::needed : DlsRepairScan::notNeeded;
}

} // namespace detail

// Reads only the RIFF header and top-level chunk-size words. No sample payload
// is read. An indeterminate scan must use the bounded full-buffer repair path.
template <typename Reader>
DlsRepairScan dlsRepairNeeded(size_t n, Reader read) {
    auto ignoreWrite = [](size_t, uint32_t) {};
    return detail::scanDlsRepairs(n, read, ignoreWrite);
}

// Repairs a "DLS " RIFF image in place. Returns true if anything changed.
inline bool repairDlsImage(uint8_t* d, size_t n) {
    auto read = [d, n](size_t offset, uint8_t* destination, size_t count) {
        if (offset > n || count > n - offset)
            return false;
        std::memcpy(destination, d + offset, count);
        return true;
    };
    auto write = [d](size_t offset, uint32_t value) {
        d[offset] = static_cast<uint8_t>(value & 0xff);
        d[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
        d[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
        d[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
    };
    return detail::scanDlsRepairs(n, read, write) == DlsRepairScan::needed;
}

} // namespace juicysf
