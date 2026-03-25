#include "qemu/osdep.h"
#include "qemu/fast-hash.h"

uint64_t fast_hash(const uint8_t *data, size_t len)
{
    const uint64_t fnv_offset = 1469598103934665603ULL;
    const uint64_t fnv_prime = 1099511628211ULL;
    uint64_t hash = fnv_offset;

    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= fnv_prime;
    }

    return hash;
}
