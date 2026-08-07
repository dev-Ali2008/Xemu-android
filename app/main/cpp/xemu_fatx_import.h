#ifndef XEMU_FATX_IMPORT_H
#define XEMU_FATX_IMPORT_H

#include "block/block-common.h"
#include "qapi/error.h"

#include <stdbool.h>

typedef struct XemuFatxDashboardStatus {
    bool xboxdash_xbe_present;
    bool msdash_xbe_present;
    bool xbox_xtf_present;
    bool xodash_dir_present;
    bool audio_dir_present;
    bool fonts_dir_present;
    bool xboxdashdata_dir_present;
} XemuFatxDashboardStatus;

bool xemu_fatx_import_dashboard(BlockBackend *blk,
                                const char *source_root,
                                const char *backup_root,
                                Error **errp);

bool xemu_fatx_inspect_retail_dashboard(BlockBackend *blk,
                                        XemuFatxDashboardStatus *status,
                                        Error **errp);

#endif
