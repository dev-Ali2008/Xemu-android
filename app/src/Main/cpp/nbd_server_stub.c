#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-export.h"
#include "block/nbd.h"

void nbd_server_start(SocketAddress *addr, uint32_t handshake_max_secs,
                      const char *tls_creds, const char *tls_authz,
                      uint32_t max_connections, Error **errp)
{
    (void)addr;
    (void)handshake_max_secs;
    (void)tls_creds;
    (void)tls_authz;
    (void)max_connections;
    error_setg(errp, "NBD server not supported on Android");
}

void nbd_server_start_options(NbdServerOptions *arg, Error **errp)
{
    (void)arg;
    error_setg(errp, "NBD server not supported on Android");
}

void qmp_nbd_server_start(bool has_handshake_max_seconds,
                          uint32_t handshake_max_seconds,
                          const char *tls_creds, const char *tls_authz,
                          bool has_max_connections, uint32_t max_connections,
                          SocketAddressLegacy *addr, Error **errp)
{
    (void)has_handshake_max_seconds;
    (void)handshake_max_seconds;
    (void)tls_creds;
    (void)tls_authz;
    (void)has_max_connections;
    (void)max_connections;
    (void)addr;
    error_setg(errp, "NBD server not supported on Android");
}

void qmp_nbd_server_add(NbdServerAddOptions *arg, Error **errp)
{
    (void)arg;
    error_setg(errp, "NBD server not supported on Android");
}

void qmp_nbd_server_remove(const char *name, bool has_mode,
                           BlockExportRemoveMode mode, Error **errp)
{
    (void)name;
    (void)has_mode;
    (void)mode;
    error_setg(errp, "NBD server not supported on Android");
}

void qmp_nbd_server_stop(Error **errp)
{
    error_setg(errp, "NBD server not supported on Android");
}
