#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qobject/qdict.h"
#include "crypto/init.h"
#include "exec/cpu-common.h"
#include "system/cpus.h"
#include "block/block-common.h"
#include "block/block-global-state.h"
#include "system/block-backend-global-state.h"
#include "system/block-backend-io.h"

#include "xemu_fatx_import.h"

#include <jni.h>
#include <pthread.h>

#define ANDROID_XBOX_HDD_MINIMUM_RETAIL_DISK_BYTES 0x1DD156000LL

#define ANDROID_XBOX_HDD_QCOW2_MAGIC 0x514649FBU
#define ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES 512ULL

#define ANDROID_XBOX_HDD_REFURB_OFFSET 0x600ULL
#define ANDROID_XBOX_HDD_REFURB_SIGNATURE 0x42524652U

#define ANDROID_XBOX_HDD_CONFIG_STATIC_PRIMARY_DNS_OFFSET 0x102CULL
#define ANDROID_XBOX_HDD_CONFIG_STATIC_SECONDARY_DNS_OFFSET 0x1030ULL
#define ANDROID_XBOX_HDD_CONFIG_XBLIVE_PRIMARY_DNS_OFFSET 0x1060ULL
#define ANDROID_XBOX_HDD_CONFIG_XBLIVE_SECONDARY_DNS_OFFSET 0x1064ULL
#define ANDROID_XBOX_HDD_CONFIG_DHCP_PRIMARY_DNS_OFFSET 0x1178ULL
#define ANDROID_XBOX_HDD_CONFIG_DHCP_SECONDARY_DNS_OFFSET 0x117CULL
#define ANDROID_XBOX_HDD_CONFIG_PPPOE_PRIMARY_DNS_OFFSET 0x118CULL
#define ANDROID_XBOX_HDD_CONFIG_PPPOE_SECONDARY_DNS_OFFSET 0x1190ULL
#define ANDROID_XBOX_HDD_CONFIG_MINIMUM_BYTES 0x1194ULL

#define ANDROID_XBOX_HDD_FATX_SIGNATURE 0x58544146U
#define ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE 4096ULL
#define ANDROID_XBOX_HDD_FATX_FAT_OFFSET ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE
#define ANDROID_XBOX_HDD_FATX_RESERVED_ENTRY_COUNT 1ULL
#define ANDROID_XBOX_HDD_FATX_RETAIL_CLUSTER_SIZE (16ULL * 1024ULL)
#define ANDROID_XBOX_HDD_FATX_NON_RETAIL_SECTORS_PER_CLUSTER 128U
#define ANDROID_XBOX_HDD_FATX_END_OF_DIR_MARKER 0xFFU

#define ANDROID_XBOX_HDD_FATX_CLUSTER_MEDIA_16 0xFFF8U
#define ANDROID_XBOX_HDD_FATX_CLUSTER_END_16 0xFFFFU
#define ANDROID_XBOX_HDD_FATX_CLUSTER_MEDIA_32 0xFFFFFFF8U
#define ANDROID_XBOX_HDD_FATX_CLUSTER_END_32 0xFFFFFFFFU

#define ANDROID_XBOX_HDD_EXTENDED_PARTITION_OFFSET 0x1DD156000ULL
#define ANDROID_XBOX_HDD_G_PARTITION_OFFSET \
    (0x0FFFFFFFULL * ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES)

typedef enum AndroidXboxHddLayout {
    ANDROID_XBOX_HDD_LAYOUT_RETAIL = 0,
    ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F = 1,
    ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F_G = 2,
} AndroidXboxHddLayout;

typedef enum AndroidXboxHddFormat {
    ANDROID_XBOX_HDD_FORMAT_RAW = 0,
    ANDROID_XBOX_HDD_FORMAT_QCOW2 = 1,
} AndroidXboxHddFormat;

typedef struct AndroidXboxHddPartition {
    char letter;
    uint64_t offset;
    uint64_t size;
} AndroidXboxHddPartition;

typedef struct AndroidXboxHddPartitionFormat {
    uint32_t volume_id;
    uint32_t sectors_per_cluster;
    uint64_t bytes_per_cluster;
    bool fat_is_16_bit;
    uint64_t fat_length_bytes;
    uint64_t cluster_offset;
} AndroidXboxHddPartitionFormat;

static const AndroidXboxHddPartition g_android_xbox_hdd_retail_partitions[] = {
    { 'X', 0x00080000ULL, 0x02EE00000ULL },
    { 'Y', 0x2EE80000ULL, 0x02EE00000ULL },
    { 'Z', 0x5DC80000ULL, 0x02EE00000ULL },
    { 'C', 0x8CA80000ULL, 0x01F400000ULL },
    { 'E', 0xABE80000ULL, 0x1312D6000ULL },
};

static pthread_mutex_t g_android_xbox_hdd_init_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_android_xbox_hdd_init_complete;
static char *g_android_xbox_hdd_init_failure;

static bool android_xbox_hdd_write_bytes(BlockBackend *blk,
                                         uint64_t offset,
                                         const void *buffer,
                                         uint64_t size,
                                         const char *label,
                                         Error **errp);

static void android_xbox_hdd_throw_exception(JNIEnv *env,
                                             const char *class_name,
                                             const char *message)
{
    jclass exception_class;

    if ((*env)->ExceptionCheck(env)) {
        return;
    }

    exception_class = (*env)->FindClass(env, class_name);
    if (!exception_class) {
        (*env)->ExceptionClear(env);
        exception_class = (*env)->FindClass(env, "java/lang/RuntimeException");
        if (!exception_class) {
            return;
        }
    }

    (*env)->ThrowNew(env, exception_class, message ? message : "Unknown error");
}

static void android_xbox_hdd_throw_error(JNIEnv *env,
                                         const char *class_name,
                                         const char *prefix,
                                         Error *err)
{
    char *message;

    if (err) {
        if (prefix) {
            message = g_strdup_printf("%s%s", prefix, error_get_pretty(err));
        } else {
            message = g_strdup(error_get_pretty(err));
        }
        error_free(err);
    } else {
        message = g_strdup(prefix ? prefix : "Unknown error");
    }

    android_xbox_hdd_throw_exception(env, class_name, message);
    g_free(message);
}

static bool android_xbox_hdd_ensure_block_layer(Error **errp)
{
    Error *local_err = NULL;

    pthread_mutex_lock(&g_android_xbox_hdd_init_lock);

    if (g_android_xbox_hdd_init_complete) {
        pthread_mutex_unlock(&g_android_xbox_hdd_init_lock);
        return true;
    }

    if (g_android_xbox_hdd_init_failure) {
        error_setg(errp, "%s", g_android_xbox_hdd_init_failure);
        pthread_mutex_unlock(&g_android_xbox_hdd_init_lock);
        return false;
    }

    if (socket_init() < 0) {
        error_setg(&local_err, "socket initialization failed");
        goto fail;
    }

    error_init("xemu-android-hdd");
    qemu_init_cpu_list();
    qemu_init_cpu_loop();
    module_call_init(MODULE_INIT_TRACE);
    qemu_init_exec_dir("xemu-android-hdd");

    if (qemu_init_main_loop(&local_err) < 0) {
        goto fail;
    }
    if (local_err) {
        goto fail;
    }

    if (qcrypto_init(&local_err) < 0) {
        goto fail;
    }
    if (local_err) {
        goto fail;
    }

    {
        BQL_LOCK_GUARD();
        module_call_init(MODULE_INIT_QOM);
        bdrv_init();
    }

    g_android_xbox_hdd_init_complete = true;
    pthread_mutex_unlock(&g_android_xbox_hdd_init_lock);
    return true;

fail:
    g_android_xbox_hdd_init_failure = g_strdup(
        local_err ? error_get_pretty(local_err) : "Unknown block layer initialization failure");
    error_propagate(errp, local_err);
    pthread_mutex_unlock(&g_android_xbox_hdd_init_lock);
    return false;
}

static bool android_xbox_hdd_sniff_format(const char *path,
                                          AndroidXboxHddFormat *format,
                                          Error **errp)
{
    FILE *file;
    uint8_t header[4];
    size_t bytes_read;
    uint32_t magic = 0;

    file = fopen(path, "rb");
    if (!file) {
        error_setg(errp, "Could not read HDD image header: %s", g_strerror(errno));
        return false;
    }

    bytes_read = fread(header, 1, sizeof(header), file);
    fclose(file);

    if (bytes_read == sizeof(header)) {
        magic = ((uint32_t)header[0] << 24) |
                ((uint32_t)header[1] << 16) |
                ((uint32_t)header[2] << 8) |
                (uint32_t)header[3];
    }

    *format = (magic == ANDROID_XBOX_HDD_QCOW2_MAGIC)
        ? ANDROID_XBOX_HDD_FORMAT_QCOW2
        : ANDROID_XBOX_HDD_FORMAT_RAW;
    return true;
}

static BlockBackend *android_xbox_hdd_open_backend(const char *path,
                                                   bool writable,
                                                   Error **errp)
{
    QDict *options = qdict_new();
    int flags = BDRV_O_NO_BACKING;

    if (writable) {
        flags |= BDRV_O_RDWR;
    }

    return blk_new_open(path, NULL, options, flags, errp);
}

static bool android_xbox_hdd_copy_ipv4(JNIEnv *env,
                                       jbyteArray array,
                                       uint8_t out[4],
                                       const char *label)
{
    jbyte buffer[4];
    jsize length;

    if (!array) {
        android_xbox_hdd_throw_exception(env, "java/lang/IllegalArgumentException",
                                         label);
        return false;
    }

    length = (*env)->GetArrayLength(env, array);
    if (length != 4) {
        android_xbox_hdd_throw_exception(env, "java/lang/IllegalArgumentException",
                                         label);
        return false;
    }

    (*env)->GetByteArrayRegion(env, array, 0, 4, buffer);
    if ((*env)->ExceptionCheck(env)) {
        return false;
    }

    for (size_t i = 0; i < 4; ++i) {
        out[i] = (uint8_t)buffer[i];
    }
    return true;
}

static bool android_xbox_hdd_inspect_locked(const char *path,
                                            AndroidXboxHddFormat *format,
                                            int64_t *total_bytes,
                                            Error **errp)
{
    BlockBackend *blk = NULL;
    int64_t length;
    bool ok = false;

    if (!android_xbox_hdd_sniff_format(path, format, errp)) {
        return false;
    }

    blk = android_xbox_hdd_open_backend(path, false, errp);
    if (!blk) {
        return false;
    }

    length = blk_getlength(blk);
    if (length < 0) {
        error_setg(errp, "Could not determine the HDD size: %s", g_strerror(-length));
        goto cleanup;
    }

    *total_bytes = length;
    ok = true;

cleanup:
    if (blk) {
        blk_drain(blk);
        blk_unref(blk);
    }
    return ok;
}

static bool android_xbox_hdd_inspect_dashboard_locked(const char *path,
                                                      jint *flags_out,
                                                      Error **errp)
{
    BlockBackend *blk = NULL;
    XemuFatxDashboardStatus status;
    bool ok = false;
    jint flags = 0;

    blk = android_xbox_hdd_open_backend(path, false, errp);
    if (!blk) {
        return false;
    }

    if (!xemu_fatx_inspect_retail_dashboard(blk, &status, errp)) {
        goto cleanup;
    }

    if (status.xboxdash_xbe_present) {
        flags |= 1 << 0;
    }
    if (status.xodash_dir_present) {
        flags |= 1 << 1;
    }
    if (status.xbox_xtf_present) {
        flags |= 1 << 2;
    }
    if (status.msdash_xbe_present) {
        flags |= 1 << 3;
    }
    if (status.audio_dir_present) {
        flags |= 1 << 4;
    }
    if (status.fonts_dir_present) {
        flags |= 1 << 5;
    }
    if (status.xboxdashdata_dir_present) {
        flags |= 1 << 6;
    }

    if (flags_out) {
        *flags_out = flags;
    }
    ok = true;

cleanup:
    if (blk) {
        blk_drain(blk);
        blk_unref(blk);
    }
    return ok;
}

static bool android_xbox_hdd_write_insignia_dns_locked(const char *path,
                                                       const uint8_t primary_dns[4],
                                                       const uint8_t secondary_dns[4],
                                                       Error **errp)
{
    static const uint64_t primary_offsets[] = {
        ANDROID_XBOX_HDD_CONFIG_STATIC_PRIMARY_DNS_OFFSET,
        ANDROID_XBOX_HDD_CONFIG_XBLIVE_PRIMARY_DNS_OFFSET,
        ANDROID_XBOX_HDD_CONFIG_DHCP_PRIMARY_DNS_OFFSET,
        ANDROID_XBOX_HDD_CONFIG_PPPOE_PRIMARY_DNS_OFFSET,
    };
    static const uint64_t secondary_offsets[] = {
        ANDROID_XBOX_HDD_CONFIG_STATIC_SECONDARY_DNS_OFFSET,
        ANDROID_XBOX_HDD_CONFIG_XBLIVE_SECONDARY_DNS_OFFSET,
        ANDROID_XBOX_HDD_CONFIG_DHCP_SECONDARY_DNS_OFFSET,
        ANDROID_XBOX_HDD_CONFIG_PPPOE_SECONDARY_DNS_OFFSET,
    };
    BlockBackend *blk = NULL;
    int64_t total_bytes;
    bool ok = false;

    blk = android_xbox_hdd_open_backend(path, true, errp);
    if (!blk) {
        return false;
    }

    total_bytes = blk_getlength(blk);
    if (total_bytes < 0) {
        error_setg(errp, "Could not determine the HDD size: %s", g_strerror(-total_bytes));
        goto cleanup;
    }
    if ((uint64_t)total_bytes < ANDROID_XBOX_HDD_CONFIG_MINIMUM_BYTES) {
        error_setg(errp, "The current HDD image is too small to contain the Xbox config sector");
        goto cleanup;
    }

    for (size_t i = 0; i < G_N_ELEMENTS(primary_offsets); ++i) {
        if (!android_xbox_hdd_write_bytes(blk, primary_offsets[i], primary_dns, 4,
                                          "Insignia primary DNS", errp) ||
            !android_xbox_hdd_write_bytes(blk, secondary_offsets[i], secondary_dns, 4,
                                          "Insignia secondary DNS", errp)) {
            goto cleanup;
        }
    }

    if (blk_flush(blk) < 0) {
        error_setg(errp, "Failed to flush Insignia DNS writes");
        goto cleanup;
    }

    ok = true;

cleanup:
    if (blk) {
        blk_drain(blk);
        blk_unref(blk);
    }
    return ok;
}

static uint32_t android_xbox_hdd_retail_sectors_per_cluster(void)
{
    return (uint32_t)(ANDROID_XBOX_HDD_FATX_RETAIL_CLUSTER_SIZE /
                      ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES);
}

static uint64_t android_xbox_hdd_aligned_extended_bytes(uint64_t total_bytes)
{
    if (total_bytes <= ANDROID_XBOX_HDD_EXTENDED_PARTITION_OFFSET) {
        return 0;
    }

    return ((total_bytes - ANDROID_XBOX_HDD_EXTENDED_PARTITION_OFFSET) /
            ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES) *
           ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES;
}

static bool android_xbox_hdd_calculate_standard_fg_layout(uint64_t total_bytes,
                                                          uint64_t *f_size,
                                                          uint64_t *g_size)
{
    uint64_t aligned_total_bytes =
        (total_bytes / ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES) *
        ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES;

    if (aligned_total_bytes <= ANDROID_XBOX_HDD_G_PARTITION_OFFSET ||
        ANDROID_XBOX_HDD_G_PARTITION_OFFSET <= ANDROID_XBOX_HDD_EXTENDED_PARTITION_OFFSET) {
        *f_size = 0;
        *g_size = 0;
        return false;
    }

    /*
     * Match the classic Xbox "67" layout that most BIOSes and dashboards
     * expect: F consumes the space up to the 28-bit LBA boundary and G
     * starts exactly at that boundary.
     */
    *f_size = ANDROID_XBOX_HDD_G_PARTITION_OFFSET -
              ANDROID_XBOX_HDD_EXTENDED_PARTITION_OFFSET;
    *g_size = aligned_total_bytes - ANDROID_XBOX_HDD_G_PARTITION_OFFSET;
    return *g_size > 0;
}

static bool android_xbox_hdd_build_partition_format(
    const AndroidXboxHddPartition *partition,
    int partition_index,
    uint32_t sectors_per_cluster,
    AndroidXboxHddPartitionFormat *format,
    Error **errp)
{
    uint64_t fat_entries;
    uint64_t fat_length_bytes;
    uint64_t bytes_per_cluster;

    bytes_per_cluster = (uint64_t)sectors_per_cluster * ANDROID_XBOX_HDD_SECTOR_SIZE_BYTES;
    if (bytes_per_cluster == 0) {
        error_setg(errp, "Invalid cluster size for partition %c", partition->letter);
        return false;
    }
    if (partition->size == 0) {
        error_setg(errp, "Partition %c has no usable space", partition->letter);
        return false;
    }

    fat_entries = partition->size / bytes_per_cluster;
    fat_entries += ANDROID_XBOX_HDD_FATX_RESERVED_ENTRY_COUNT;

    format->fat_is_16_bit = fat_entries < 0xFFF0ULL;
    fat_length_bytes = fat_entries * (format->fat_is_16_bit ? 2ULL : 4ULL);
    if (fat_length_bytes % ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE != 0) {
        fat_length_bytes += ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE -
            (fat_length_bytes % ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE);
    }
    if (ANDROID_XBOX_HDD_FATX_FAT_OFFSET + fat_length_bytes + bytes_per_cluster >
        partition->size) {
        error_setg(errp, "Partition %c is too small for the selected layout",
                   partition->letter);
        return false;
    }

    format->volume_id = g_random_int() ^
                        (uint32_t)partition_index ^
                        (uint32_t)(unsigned char)partition->letter;
    format->sectors_per_cluster = sectors_per_cluster;
    format->bytes_per_cluster = bytes_per_cluster;
    format->fat_length_bytes = fat_length_bytes;
    format->cluster_offset = partition->offset +
                             ANDROID_XBOX_HDD_FATX_FAT_OFFSET +
                             fat_length_bytes;
    return true;
}

static bool android_xbox_hdd_can_format_partition(uint64_t size,
                                                  uint32_t sectors_per_cluster)
{
    AndroidXboxHddPartition partition = {
        .letter = '?',
        .offset = 0,
        .size = size,
    };
    AndroidXboxHddPartitionFormat format;
    Error *err = NULL;
    bool ok;

    ok = android_xbox_hdd_build_partition_format(&partition, 0,
                                                 sectors_per_cluster,
                                                 &format, &err);
    error_free(err);
    return ok;
}

static bool android_xbox_hdd_layout_supported(uint64_t total_bytes,
                                              AndroidXboxHddLayout layout)
{
    uint64_t extended_bytes;
    uint64_t f_size;
    uint64_t g_size;

    if (total_bytes < ANDROID_XBOX_HDD_MINIMUM_RETAIL_DISK_BYTES) {
        return false;
    }

    extended_bytes = android_xbox_hdd_aligned_extended_bytes(total_bytes);

    switch (layout) {
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL:
        return true;
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F:
        return android_xbox_hdd_can_format_partition(
            extended_bytes, ANDROID_XBOX_HDD_FATX_NON_RETAIL_SECTORS_PER_CLUSTER);
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F_G:
        if (!android_xbox_hdd_calculate_standard_fg_layout(total_bytes, &f_size, &g_size)) {
            return false;
        }
        return android_xbox_hdd_can_format_partition(
                   f_size, ANDROID_XBOX_HDD_FATX_NON_RETAIL_SECTORS_PER_CLUSTER) &&
               android_xbox_hdd_can_format_partition(
                   g_size, ANDROID_XBOX_HDD_FATX_NON_RETAIL_SECTORS_PER_CLUSTER);
    default:
        return false;
    }
}

static void android_xbox_hdd_write_le16(uint8_t *buffer, size_t offset, uint16_t value)
{
    buffer[offset] = (uint8_t)(value & 0xFFU);
    buffer[offset + 1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void android_xbox_hdd_write_le32(uint8_t *buffer, size_t offset, uint32_t value)
{
    buffer[offset] = (uint8_t)(value & 0xFFU);
    buffer[offset + 1] = (uint8_t)((value >> 8) & 0xFFU);
    buffer[offset + 2] = (uint8_t)((value >> 16) & 0xFFU);
    buffer[offset + 3] = (uint8_t)((value >> 24) & 0xFFU);
}

static bool android_xbox_hdd_write_bytes(BlockBackend *blk,
                                         uint64_t offset,
                                         const void *buffer,
                                         uint64_t size,
                                         const char *label,
                                         Error **errp)
{
    int ret = blk_pwrite(blk, (int64_t)offset, (int64_t)size, buffer, 0);

    if (ret < 0) {
        error_setg(errp, "Failed to write %s at 0x%" PRIx64 ": %s",
                   label, offset, g_strerror(-ret));
        return false;
    }

    return true;
}

static bool android_xbox_hdd_zero_range(BlockBackend *blk,
                                        uint64_t offset,
                                        uint64_t length,
                                        Error **errp)
{
    static const size_t chunk_size = 64 * 1024;
    uint8_t *zeroes;
    uint64_t remaining = length;
    uint64_t current_offset = offset;
    bool ok = true;

    if (length == 0) {
        return true;
    }

    zeroes = g_malloc0(chunk_size);
    while (remaining > 0) {
        uint64_t bytes_to_write = MIN((uint64_t)chunk_size, remaining);
        if (!android_xbox_hdd_write_bytes(blk, current_offset, zeroes,
                                          bytes_to_write, "zeroed FAT region", errp)) {
            ok = false;
            break;
        }
        current_offset += bytes_to_write;
        remaining -= bytes_to_write;
    }
    g_free(zeroes);
    return ok;
}

static bool android_xbox_hdd_write_refurb_info(BlockBackend *blk, Error **errp)
{
    uint8_t buffer[16] = { 0 };

    android_xbox_hdd_write_le32(buffer, 0, ANDROID_XBOX_HDD_REFURB_SIGNATURE);
    return android_xbox_hdd_write_bytes(blk, ANDROID_XBOX_HDD_REFURB_OFFSET,
                                        buffer, sizeof(buffer),
                                        "refurb info", errp);
}

static bool android_xbox_hdd_write_superblock(
    BlockBackend *blk,
    const AndroidXboxHddPartition *partition,
    const AndroidXboxHddPartitionFormat *format,
    Error **errp)
{
    uint8_t *superblock = g_malloc(ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE);
    bool ok;

    memset(superblock, 0xFF, ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE);
    android_xbox_hdd_write_le32(superblock, 0, ANDROID_XBOX_HDD_FATX_SIGNATURE);
    android_xbox_hdd_write_le32(superblock, 4, format->volume_id);
    android_xbox_hdd_write_le32(superblock, 8, format->sectors_per_cluster);
    android_xbox_hdd_write_le32(superblock, 12, 1);
    android_xbox_hdd_write_le16(superblock, 16, 0);

    ok = android_xbox_hdd_write_bytes(blk, partition->offset, superblock,
                                      ANDROID_XBOX_HDD_FATX_SUPERBLOCK_SIZE,
                                      "FATX superblock", errp);
    g_free(superblock);
    return ok;
}

static bool android_xbox_hdd_write_initial_fat_entries(
    BlockBackend *blk,
    const AndroidXboxHddPartition *partition,
    const AndroidXboxHddPartitionFormat *format,
    Error **errp)
{
    uint8_t entries[8] = { 0 };
    uint64_t bytes = format->fat_is_16_bit ? 4 : 8;

    if (format->fat_is_16_bit) {
        android_xbox_hdd_write_le16(entries, 0, ANDROID_XBOX_HDD_FATX_CLUSTER_MEDIA_16);
        android_xbox_hdd_write_le16(entries, 2, ANDROID_XBOX_HDD_FATX_CLUSTER_END_16);
    } else {
        android_xbox_hdd_write_le32(entries, 0, ANDROID_XBOX_HDD_FATX_CLUSTER_MEDIA_32);
        android_xbox_hdd_write_le32(entries, 4, ANDROID_XBOX_HDD_FATX_CLUSTER_END_32);
    }

    return android_xbox_hdd_write_bytes(
        blk,
        partition->offset + ANDROID_XBOX_HDD_FATX_FAT_OFFSET,
        entries,
        bytes,
        "initial FAT entries",
        errp);
}

static bool android_xbox_hdd_fill_root_directory(
    BlockBackend *blk,
    const AndroidXboxHddPartitionFormat *format,
    Error **errp)
{
    uint8_t *root_data = g_malloc(format->bytes_per_cluster);
    bool ok;

    memset(root_data, ANDROID_XBOX_HDD_FATX_END_OF_DIR_MARKER,
           format->bytes_per_cluster);
    ok = android_xbox_hdd_write_bytes(blk, format->cluster_offset, root_data,
                                      format->bytes_per_cluster,
                                      "root directory cluster", errp);
    g_free(root_data);
    return ok;
}

static bool android_xbox_hdd_format_partition(
    BlockBackend *blk,
    const AndroidXboxHddPartition *partition,
    int partition_index,
    uint32_t sectors_per_cluster,
    Error **errp)
{
    AndroidXboxHddPartitionFormat format;
    bool ok = false;

    if (!android_xbox_hdd_build_partition_format(partition, partition_index,
                                                 sectors_per_cluster, &format, errp)) {
        return false;
    }

    if (!android_xbox_hdd_write_superblock(blk, partition, &format, errp)) {
        goto cleanup;
    }

    if (!android_xbox_hdd_zero_range(blk,
                                     partition->offset + ANDROID_XBOX_HDD_FATX_FAT_OFFSET,
                                     format.fat_length_bytes,
                                     errp)) {
        goto cleanup;
    }

    if (!android_xbox_hdd_write_initial_fat_entries(blk, partition, &format, errp)) {
        goto cleanup;
    }

    if (!android_xbox_hdd_fill_root_directory(blk, &format, errp)) {
        goto cleanup;
    }

    ok = true;

cleanup:
    if (!ok && errp && *errp) {
        error_prepend(errp, "Partition %c: ", partition->letter);
    }
    return ok;
}

static bool android_xbox_hdd_initialize_locked(const char *path,
                                               AndroidXboxHddLayout layout,
                                               Error **errp)
{
    BlockBackend *blk = NULL;
    int64_t total_bytes;
    uint64_t extended_bytes;
    uint64_t f_size;
    uint64_t g_size;
    int ret;
    bool ok = false;

    blk = android_xbox_hdd_open_backend(path, true, errp);
    if (!blk) {
        return false;
    }

    total_bytes = blk_getlength(blk);
    if (total_bytes < 0) {
        error_setg(errp, "Could not determine the HDD size: %s", g_strerror(-total_bytes));
        goto cleanup;
    }

    if (total_bytes < ANDROID_XBOX_HDD_MINIMUM_RETAIL_DISK_BYTES) {
        error_setg(errp, "The current HDD image is smaller than a stock Xbox disk");
        goto cleanup;
    }

    if (!android_xbox_hdd_layout_supported((uint64_t)total_bytes, layout)) {
        error_setg(errp,
                   "The current HDD image does not have enough usable space for the selected layout");
        goto cleanup;
    }

    if (!android_xbox_hdd_write_refurb_info(blk, errp)) {
        goto cleanup;
    }

    for (size_t i = 0; i < G_N_ELEMENTS(g_android_xbox_hdd_retail_partitions); ++i) {
        if (!android_xbox_hdd_format_partition(
                blk,
                &g_android_xbox_hdd_retail_partitions[i],
                (int)i,
                android_xbox_hdd_retail_sectors_per_cluster(),
                errp)) {
            goto cleanup;
        }
    }

    extended_bytes = android_xbox_hdd_aligned_extended_bytes((uint64_t)total_bytes);
    switch (layout) {
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL:
        break;
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F: {
        AndroidXboxHddPartition partition_f = {
            .letter = 'F',
            .offset = ANDROID_XBOX_HDD_EXTENDED_PARTITION_OFFSET,
            .size = extended_bytes,
        };
        if (!android_xbox_hdd_format_partition(
                blk,
                &partition_f,
                (int)G_N_ELEMENTS(g_android_xbox_hdd_retail_partitions),
                ANDROID_XBOX_HDD_FATX_NON_RETAIL_SECTORS_PER_CLUSTER,
                errp)) {
            goto cleanup;
        }
        break;
    }
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F_G: {
        AndroidXboxHddPartition partition_f;
        AndroidXboxHddPartition partition_g;

        if (!android_xbox_hdd_calculate_standard_fg_layout(
                (uint64_t)total_bytes, &f_size, &g_size)) {
            error_setg(errp,
                       "The current HDD image does not have enough usable space for a standard F + G layout");
            goto cleanup;
        }
        partition_f.letter = 'F';
        partition_f.offset = ANDROID_XBOX_HDD_EXTENDED_PARTITION_OFFSET;
        partition_f.size = f_size;
        partition_g.letter = 'G';
        partition_g.offset = ANDROID_XBOX_HDD_G_PARTITION_OFFSET;
        partition_g.size = g_size;

        if (!android_xbox_hdd_format_partition(
                blk,
                &partition_f,
                (int)G_N_ELEMENTS(g_android_xbox_hdd_retail_partitions),
                ANDROID_XBOX_HDD_FATX_NON_RETAIL_SECTORS_PER_CLUSTER,
                errp)) {
            goto cleanup;
        }
        if (!android_xbox_hdd_format_partition(
                blk,
                &partition_g,
                (int)G_N_ELEMENTS(g_android_xbox_hdd_retail_partitions) + 1,
                ANDROID_XBOX_HDD_FATX_NON_RETAIL_SECTORS_PER_CLUSTER,
                errp)) {
            goto cleanup;
        }
        break;
    }
    default:
        error_setg(errp, "Unknown HDD layout");
        goto cleanup;
    }

    ret = blk_flush(blk);
    if (ret < 0) {
        error_setg(errp, "Failed to flush HDD image writes: %s", g_strerror(-ret));
        goto cleanup;
    }

    ok = true;

cleanup:
    if (blk) {
        blk_drain(blk);
        blk_unref(blk);
    }
    return ok;
}

JNIEXPORT jlongArray JNICALL
Java_Ali_Xanite_XboxHddFormatter_00024NativeBridge_nativeInspectHdd(
        JNIEnv *env, jobject obj, jstring jpath)
{
    const char *path;
    Error *err = NULL;
    AndroidXboxHddFormat format;
    int64_t total_bytes = 0;
    jlongArray result = NULL;
    jlong values[2];

    (void)obj;

    if (!jpath) {
        android_xbox_hdd_throw_exception(env, "java/io/IOException",
                                         "No local HDD image is configured.");
        return NULL;
    }

    path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) {
        return NULL;
    }

    if (!android_xbox_hdd_ensure_block_layer(&err)) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        android_xbox_hdd_throw_error(env, "java/io/IOException",
                                     "Failed to initialize HDD tools: ", err);
        return NULL;
    }

    {
        BQL_LOCK_GUARD();
        if (!android_xbox_hdd_inspect_locked(path, &format, &total_bytes, &err)) {
            (*env)->ReleaseStringUTFChars(env, jpath, path);
            android_xbox_hdd_throw_error(env, "java/io/IOException", NULL, err);
            return NULL;
        }
    }

    (*env)->ReleaseStringUTFChars(env, jpath, path);

    values[0] = (jlong)format;
    values[1] = (jlong)total_bytes;
    result = (*env)->NewLongArray(env, 2);
    if (!result) {
        return NULL;
    }
    (*env)->SetLongArrayRegion(env, result, 0, 2, values);
    return result;
}

JNIEXPORT void JNICALL
Java_Ali_Xanite_XboxHddFormatter_00024NativeBridge_nativeInitializeHdd(
        JNIEnv *env, jobject obj, jstring jpath, jint layout_ordinal)
{
    const char *path;
    Error *err = NULL;
    AndroidXboxHddLayout layout;

    (void)obj;

    if (!jpath) {
        android_xbox_hdd_throw_exception(env, "java/io/IOException",
                                         "No local HDD image is configured.");
        return;
    }

    switch (layout_ordinal) {
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL:
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F:
    case ANDROID_XBOX_HDD_LAYOUT_RETAIL_PLUS_F_G:
        layout = (AndroidXboxHddLayout)layout_ordinal;
        break;
    default:
        android_xbox_hdd_throw_exception(env,
                                         "java/lang/IllegalArgumentException",
                                         "Unknown HDD layout");
        return;
    }

    path = (*env)->GetStringUTFChars(env, jpath, NULL);
    if (!path) {
        return;
    }

    if (!android_xbox_hdd_ensure_block_layer(&err)) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        android_xbox_hdd_throw_error(env, "java/io/IOException",
                                     "Failed to initialize HDD tools: ", err);
        return;
    }

    {
        BQL_LOCK_GUARD();
        if (!android_xbox_hdd_initialize_locked(path, layout, &err)) {
            (*env)->ReleaseStringUTFChars(env, jpath, path);
            android_xbox_hdd_throw_error(env, "java/io/IOException", NULL, err);
            return;
        }
    }

    (*env)->ReleaseStringUTFChars(env, jpath, path);
}

JNIEXPORT jint JNICALL
Java_Ali_Xanite_XboxInsigniaHelper_00024NativeBridge_nativeInspectDashboardFlags(
        JNIEnv *env, jobject obj, jstring jhdd_path)
{
    const char *hdd_path;
    Error *err = NULL;
    jint flags = 0;

    (void)obj;

    if (!jhdd_path) {
        android_xbox_hdd_throw_exception(env, "java/io/IOException",
                                         "No local HDD image is configured.");
        return 0;
    }

    hdd_path = (*env)->GetStringUTFChars(env, jhdd_path, NULL);
    if (!hdd_path) {
        return 0;
    }

    if (!android_xbox_hdd_ensure_block_layer(&err)) {
        (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
        android_xbox_hdd_throw_error(env, "java/io/IOException",
                                     "Failed to initialize HDD tools: ", err);
        return 0;
    }

    {
        BQL_LOCK_GUARD();
        if (!android_xbox_hdd_inspect_dashboard_locked(hdd_path, &flags, &err)) {
            (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
            android_xbox_hdd_throw_error(env, "java/io/IOException", NULL, err);
            return 0;
        }
    }

    (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
    return flags;
}

JNIEXPORT void JNICALL
Java_Ali_Xanite_XboxInsigniaHelper_00024NativeBridge_nativeApplyConfigSectorDns(
        JNIEnv *env,
        jobject obj,
        jstring jhdd_path,
        jbyteArray jprimary_dns,
        jbyteArray jsecondary_dns)
{
    const char *hdd_path;
    uint8_t primary_dns[4];
    uint8_t secondary_dns[4];
    Error *err = NULL;

    (void)obj;

    if (!jhdd_path) {
        android_xbox_hdd_throw_exception(env, "java/io/IOException",
                                         "No local HDD image is configured.");
        return;
    }

    if (!android_xbox_hdd_copy_ipv4(env, jprimary_dns, primary_dns,
                                    "Insignia primary DNS must be an IPv4 address.") ||
        !android_xbox_hdd_copy_ipv4(env, jsecondary_dns, secondary_dns,
                                    "Insignia secondary DNS must be an IPv4 address.")) {
        return;
    }

    hdd_path = (*env)->GetStringUTFChars(env, jhdd_path, NULL);
    if (!hdd_path) {
        return;
    }

    if (!android_xbox_hdd_ensure_block_layer(&err)) {
        (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
        android_xbox_hdd_throw_error(env, "java/io/IOException",
                                     "Failed to initialize HDD tools: ", err);
        return;
    }

    {
        BQL_LOCK_GUARD();
        if (!android_xbox_hdd_write_insignia_dns_locked(hdd_path,
                                                        primary_dns,
                                                        secondary_dns,
                                                        &err)) {
            (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
            android_xbox_hdd_throw_error(env, "java/io/IOException", NULL, err);
            return;
        }
    }

    (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
}

JNIEXPORT void JNICALL
Java_Ali_Xanite_XboxDashboardImporter_00024NativeBridge_nativeImportDashboard(
        JNIEnv *env, jobject obj, jstring jhdd_path, jstring jsource_root, jstring jbackup_root)
{
    const char *hdd_path;
    const char *source_root;
    const char *backup_root;
    BlockBackend *blk = NULL;
    Error *err = NULL;

    (void)obj;

    if (!jhdd_path || !jsource_root || !jbackup_root) {
        android_xbox_hdd_throw_exception(env, "java/io/IOException",
                                         "Missing HDD path, dashboard source, or backup path.");
        return;
    }

    hdd_path = (*env)->GetStringUTFChars(env, jhdd_path, NULL);
    source_root = (*env)->GetStringUTFChars(env, jsource_root, NULL);
    backup_root = (*env)->GetStringUTFChars(env, jbackup_root, NULL);
    if (!hdd_path || !source_root || !backup_root) {
        if (hdd_path) {
            (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
        }
        if (source_root) {
            (*env)->ReleaseStringUTFChars(env, jsource_root, source_root);
        }
        if (backup_root) {
            (*env)->ReleaseStringUTFChars(env, jbackup_root, backup_root);
        }
        return;
    }

    if (!android_xbox_hdd_ensure_block_layer(&err)) {
        (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
        (*env)->ReleaseStringUTFChars(env, jsource_root, source_root);
        (*env)->ReleaseStringUTFChars(env, jbackup_root, backup_root);
        android_xbox_hdd_throw_error(env, "java/io/IOException",
                                     "Failed to initialize HDD tools: ", err);
        return;
    }

    {
        BQL_LOCK_GUARD();

        blk = android_xbox_hdd_open_backend(hdd_path, true, &err);
        if (!blk) {
            goto cleanup;
        }

        if (!xemu_fatx_import_dashboard(blk, source_root, backup_root, &err)) {
            goto cleanup;
        }

        if (blk_flush(blk) < 0) {
            error_setg(&err, "Failed to flush dashboard import writes");
            goto cleanup;
        }
    }

cleanup:
    if (blk) {
        blk_drain(blk);
        blk_unref(blk);
    }

    (*env)->ReleaseStringUTFChars(env, jhdd_path, hdd_path);
    (*env)->ReleaseStringUTFChars(env, jsource_root, source_root);
    (*env)->ReleaseStringUTFChars(env, jbackup_root, backup_root);

    if (err) {
        android_xbox_hdd_throw_error(env, "java/io/IOException", NULL, err);
    }
}
