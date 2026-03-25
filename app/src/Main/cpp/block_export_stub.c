#include "qemu/osdep.h"
#include "block/export.h"
#include "qapi/error.h"

static int blk_exp_nbd_create(BlockExport *exp, BlockExportOptions *opts,
                              Error **errp)
{
    (void)exp;
    (void)opts;
    error_setg(errp, "NBD export not supported on Android");
    return -ENOTSUP;
}

const BlockExportDriver blk_exp_nbd = {
    .type = BLOCK_EXPORT_TYPE_NBD,
    .instance_size = sizeof(BlockExport),
    .create = blk_exp_nbd_create,
};
