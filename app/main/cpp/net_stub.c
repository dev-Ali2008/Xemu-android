#include "qemu/osdep.h"
#include "qapi/error.h"
#include "net/clients.h"
#include "net/vhost_net.h"

VHostNetState *get_vhost_net(NetClientState *nc)
{
    (void)nc;
    return NULL;
}

int net_init_tap(const Netdev *netdev, const char *name,
                 NetClientState *peer, Error **errp)
{
    (void)netdev;
    (void)name;
    (void)peer;
    error_setg(errp, "tap networking not supported on Android");
    return -ENOSYS;
}

int net_init_bridge(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp)
{
    (void)netdev;
    (void)name;
    (void)peer;
    error_setg(errp, "bridge networking not supported on Android");
    return -ENOSYS;
}

int net_init_pcap(const Netdev *netdev, const char *name,
                  NetClientState *peer, Error **errp)
{
    (void)netdev;
    (void)name;
    (void)peer;
    error_setg(errp, "pcap networking not supported on Android");
    return -ENOSYS;
}
