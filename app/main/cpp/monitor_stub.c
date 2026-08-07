#include "qemu/osdep.h"
#include "monitor/monitor-internal.h"

HMPCommand hmp_cmds[] = {
    { NULL }
};

int hmp_compare_cmd(const char *name, const char *list)
{
    (void)name;
    (void)list;
    return 0;
}

int get_monitor_def(Monitor *mon, int64_t *pval, const char *name)
{
    (void)mon;
    (void)pval;
    (void)name;
    return -1;
}

void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp))
{
    (void)name;
    (void)handler;
}

void monitor_register_hmp(const char *name, bool info,
                          void (*cmd)(Monitor *mon, const QDict *qdict))
{
    (void)name;
    (void)info;
    (void)cmd;
}
