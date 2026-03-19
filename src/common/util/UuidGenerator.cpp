#include "UuidGenerator.h"

#include <cstdio>
#include <random>

namespace sos {

std::string generateUuid() {
    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist;

    uint32_t data[4];
    for (auto& d : data) d = dist(gen);

    // UUID v4: version = 4 (bits 15-12 of data[1]), variant = 10xx (bits 31-30 of data[2])
    data[1] = (data[1] & 0xFFFF0FFFu) | 0x00004000u;
    data[2] = (data[2] & 0x3FFFFFFFu) | 0x80000000u;

    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
        data[0],
        data[1] >> 16,
        data[1] & 0xFFFFu,
        data[2] >> 16,
        data[2] & 0xFFFFu,
        data[3]);

    return buf;
}

} // namespace sos
