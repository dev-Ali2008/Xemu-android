#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-types-run-state.h"

void qmp_watchdog_set_action(WatchdogAction action, Error **errp)
{
    (void)action;
    error_setg(errp, "watchdog not supported on Android");
}
