#include "qemu/osdep.h"
#include "hw/i386/kvm/clock.h"

void kvmclock_create(bool create_always)
{
    (void)create_always;
}
