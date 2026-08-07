#include "qemu/fast-hash.h"
#include <xxhash.h>

#ifdef __ANDROID__
static uint64_t fast_hash_android_tiny(const uint8_t *data, size_t len)
{
    const uint64_t fnv_offset = 1469598103934665603ULL;
    const uint64_t fnv_prime = 1099511628211ULL;
    uint64_t hash = fnv_offset;

    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= fnv_prime;
    }

    return hash;
}
#endif

uint64_t fast_hash(const uint8_t *data, size_t len)
{
#ifdef __ANDROID__
    /*
     * XXH3 is a clear win for the larger shader-state keys Android hashes,
     * but very small TCG blocks can spend more time in XXH3 setup than in the
     * hash itself. Keep a tiny-input fast path for those cases.
     */
    if (len <= 4) {
        return fast_hash_android_tiny(data, len);
    }
#endif
    return XXH3_64bits(data, len);
}
