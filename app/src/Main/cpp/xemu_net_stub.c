#include "qemu/osdep.h"
#include "xemu-net.h"
#include "xemu-settings.h"

void xemu_net_enable(void)
{
    g_config.net.enable = true;
}

void xemu_net_disable(void)
{
    g_config.net.enable = false;
}

int xemu_net_is_enabled(void)
{
    return g_config.net.enable ? 1 : 0;
}
