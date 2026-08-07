#include "qemu/osdep.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "crypto/random.h"

int qcrypto_random_init(Error **errp)
{
    (void)errp;
    return 0;
}

static void fill_fallback(void *buf, size_t len)
{
    uint64_t seed = (uint64_t)(uintptr_t)buf ^ (uint64_t)getpid();
    seed ^= (uint64_t)time(NULL);
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < len; ++i) {
        seed = seed * 6364136223846793005ULL + 1;
        out[i] = (uint8_t)(seed >> 32);
    }
}

int qcrypto_random_bytes(void *buf, size_t buflen, Error **errp)
{
    (void)errp;
    if (buflen == 0) {
        return 0;
    }

    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t remaining = buflen;
        uint8_t *out = (uint8_t *)buf;
        while (remaining > 0) {
            ssize_t got = read(fd, out, remaining);
            if (got > 0) {
                out += got;
                remaining -= (size_t)got;
                continue;
            }
            if (got < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        close(fd);
        if (remaining == 0) {
            return 0;
        }
    }

    fill_fallback(buf, buflen);
    return 0;
}
