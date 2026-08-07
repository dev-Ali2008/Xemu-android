#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/block-common.h"
#include "system/block-backend-io.h"

#include "xemu_fatx_import.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>

/*
 * FATX importer for Android HDD tools.
 *
 * The FATX filesystem structures and flow here are adapted to match the
 * layout already written by the Android formatter in this repo: a 4 KiB
 * superblock, a single FAT, and data clusters immediately after that FAT.
 */

#define XEMU_FATX_SIGNATURE 0x58544146U
#define XEMU_FATX_SUPERBLOCK_SIZE 4096ULL
#define XEMU_FATX_FAT_OFFSET XEMU_FATX_SUPERBLOCK_SIZE
#define XEMU_FATX_FAT_RESERVED_ENTRIES_COUNT 1U
#define XEMU_FATX_SECTOR_SIZE 512ULL
#define XEMU_FATX_MAX_FILENAME_LEN 42
#define XEMU_FATX_END_OF_DIR_MARKER 0xFFU
#define XEMU_FATX_END_OF_DIR_MARKER2 0x00U
#define XEMU_FATX_DELETED_FILE_MARKER 0xE5U
#define XEMU_FATX_ATTR_DIRECTORY (1U << 4)
#define XEMU_FATX_ATTR_MASK 0x0FU
#define XEMU_FATX_FAT_TYPE_16 16
#define XEMU_FATX_FAT_TYPE_32 32
#define XEMU_FATX_FAT_CACHE_NUM_ENTRIES 512U

#define XEMU_FATX_CLUSTER_AVAILABLE 0
#define XEMU_FATX_CLUSTER_DATA 1
#define XEMU_FATX_CLUSTER_RESERVED 2
#define XEMU_FATX_CLUSTER_BAD 3
#define XEMU_FATX_CLUSTER_MEDIA 4
#define XEMU_FATX_CLUSTER_END 5
#define XEMU_FATX_CLUSTER_INVALID 6

#define XEMU_FATX_STATUS_FILE_NOT_FOUND -2
#define XEMU_FATX_STATUS_ERROR -1
#define XEMU_FATX_STATUS_SUCCESS 0
#define XEMU_FATX_STATUS_FILE_DELETED 1
#define XEMU_FATX_STATUS_END_OF_DIR 2

#define XEMU_XBOX_HDD_PARTITION_C_OFFSET 0x8CA80000ULL
#define XEMU_XBOX_HDD_PARTITION_C_SIZE 0x01F400000ULL
#define XEMU_XBOX_HDD_PARTITION_E_OFFSET 0xABE80000ULL
#define XEMU_XBOX_HDD_PARTITION_E_SIZE 0x1312D6000ULL

typedef struct XemuFatxCache {
    uint32_t position;
    uint32_t entries;
    uint32_t entry_size;
    bool dirty;
    void *data;
    size_t data_capacity;
} XemuFatxCache;

typedef struct XemuFatxFs {
    BlockBackend *blk;
    uint64_t partition_offset;
    uint64_t partition_size;
    uint32_t volume_id;
    uint32_t fat_entry_count;
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint32_t fat_type;
    uint64_t fat_offset;
    uint64_t fat_size;
    uint64_t cluster_offset;
    uint64_t bytes_per_cluster;
    uint64_t cursor;
    uint32_t next_alloc_cluster;
    XemuFatxCache fat_cache;
    char *error_message;
} XemuFatxFs;

typedef struct XemuFatxDir {
    uint32_t cluster;
    uint32_t entry;
} XemuFatxDir;

typedef struct XemuFatxDirent {
    char filename[XEMU_FATX_MAX_FILENAME_LEN + 1];
} XemuFatxDirent;

typedef struct XemuFatxTs {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} XemuFatxTs;

typedef struct XemuFatxAttr {
    char filename[XEMU_FATX_MAX_FILENAME_LEN + 1];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t file_size;
    XemuFatxTs modified;
    XemuFatxTs created;
    XemuFatxTs accessed;
} XemuFatxAttr;

#pragma pack(push, 1)
typedef struct XemuFatxSuperblock {
    uint32_t signature;
    uint32_t volume_id;
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint16_t unknown1;
    uint8_t padding[4078];
} XemuFatxSuperblock;

typedef struct XemuFatxRawDirectoryEntry {
    uint8_t filename_len;
    uint8_t attributes;
    char filename[XEMU_FATX_MAX_FILENAME_LEN];
    uint32_t first_cluster;
    uint32_t file_size;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t created_time;
    uint16_t created_date;
    uint16_t accessed_time;
    uint16_t accessed_date;
} XemuFatxRawDirectoryEntry;
#pragma pack(pop)

static void xemu_fatx_set_error(XemuFatxFs *fs, const char *format, ...)
{
    va_list ap;

    if (fs->error_message) {
        return;
    }

    va_start(ap, format);
    fs->error_message = g_strdup_vprintf(format, ap);
    va_end(ap);
}

static void xemu_fatx_propagate_error(XemuFatxFs *fs, Error **errp, const char *prefix)
{
    const char *message = fs->error_message ? fs->error_message : "Unknown FATX error";

    if (prefix) {
        error_setg(errp, "%s%s", prefix, message);
    } else {
        error_setg(errp, "%s", message);
    }
}

static bool xemu_fatx_ascii_equal_n(const char *left, const char *right, size_t len)
{
    size_t i;

    for (i = 0; i < len; ++i) {
        if (!left[i] || !right[i]) {
            return false;
        }
        if (g_ascii_tolower(left[i]) != g_ascii_tolower(right[i])) {
            return false;
        }
    }

    return true;
}

static bool xemu_fatx_ascii_equal(const char *left, const char *right)
{
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);

    return left_len == right_len && xemu_fatx_ascii_equal_n(left, right, left_len);
}

static bool xemu_fatx_ascii_starts_with(const char *value, const char *prefix)
{
    size_t prefix_len = strlen(prefix);

    return strlen(value) >= prefix_len &&
           xemu_fatx_ascii_equal_n(value, prefix, prefix_len);
}

static bool xemu_fatx_read_bytes(BlockBackend *blk,
                                 uint64_t offset,
                                 void *buffer,
                                 size_t size,
                                 Error **errp)
{
    int ret = blk_pread(blk, (int64_t)offset, (int64_t)size, buffer, 0);

    if (ret < 0) {
        error_setg(errp, "Failed to read HDD data at 0x%" PRIx64 ": %s",
                   offset, g_strerror(-ret));
        return false;
    }

    return true;
}

static bool xemu_fatx_write_bytes(BlockBackend *blk,
                                  uint64_t offset,
                                  const void *buffer,
                                  size_t size,
                                  Error **errp)
{
    int ret = blk_pwrite(blk, (int64_t)offset, (int64_t)size, buffer, 0);

    if (ret < 0) {
        error_setg(errp, "Failed to write HDD data at 0x%" PRIx64 ": %s",
                   offset, g_strerror(-ret));
        return false;
    }

    return true;
}

static bool xemu_fatx_dev_seek(XemuFatxFs *fs, uint64_t offset)
{
    fs->cursor = offset;
    return true;
}

static bool xemu_fatx_dev_seek_cluster(XemuFatxFs *fs, uint32_t cluster, uint64_t offset)
{
    uint64_t position;

    if (cluster < XEMU_FATX_FAT_RESERVED_ENTRIES_COUNT ||
        cluster >= fs->fat_entry_count) {
        xemu_fatx_set_error(fs, "Cluster %u is out of range", cluster);
        return false;
    }

    position = fs->cluster_offset +
               ((uint64_t)cluster - XEMU_FATX_FAT_RESERVED_ENTRIES_COUNT) * fs->bytes_per_cluster +
               offset;
    if (position >= fs->partition_offset + fs->partition_size) {
        xemu_fatx_set_error(fs, "Cluster %u overruns the partition", cluster);
        return false;
    }

    fs->cursor = position;
    return true;
}

static size_t xemu_fatx_dev_read(XemuFatxFs *fs, void *buffer, size_t size, size_t items)
{
    size_t total = size * items;
    Error *err = NULL;

    if (!xemu_fatx_read_bytes(fs->blk, fs->cursor, buffer, total, &err)) {
        xemu_fatx_set_error(fs, "%s", error_get_pretty(err));
        error_free(err);
        return 0;
    }

    fs->cursor += total;
    return items;
}

static size_t xemu_fatx_dev_write(XemuFatxFs *fs, const void *buffer, size_t size, size_t items)
{
    size_t total = size * items;
    Error *err = NULL;

    if (!xemu_fatx_write_bytes(fs->blk, fs->cursor, buffer, total, &err)) {
        xemu_fatx_set_error(fs, "%s", error_get_pretty(err));
        error_free(err);
        return 0;
    }

    fs->cursor += total;
    return items;
}

static bool xemu_fatx_open_fs(XemuFatxFs *fs,
                              BlockBackend *blk,
                              uint64_t partition_offset,
                              uint64_t partition_size,
                              Error **errp)
{
    XemuFatxSuperblock superblock;
    uint64_t bytes_per_cluster;
    uint64_t fat_entries;

    memset(fs, 0, sizeof(*fs));
    fs->blk = blk;
    fs->partition_offset = partition_offset;
    fs->partition_size = partition_size;
    fs->next_alloc_cluster = 2;

    if (!xemu_fatx_read_bytes(blk, partition_offset, &superblock, sizeof(superblock), errp)) {
        return false;
    }

    if (superblock.signature != XEMU_FATX_SIGNATURE) {
        error_setg(errp, "Partition at 0x%" PRIx64 " is not formatted as FATX",
                   partition_offset);
        return false;
    }

    bytes_per_cluster = (uint64_t)superblock.sectors_per_cluster * XEMU_FATX_SECTOR_SIZE;
    if (bytes_per_cluster == 0 || superblock.sectors_per_cluster > 1024) {
        error_setg(errp, "Partition at 0x%" PRIx64 " has an invalid FATX cluster size",
                   partition_offset);
        return false;
    }

    fat_entries = partition_size / bytes_per_cluster;
    fat_entries += XEMU_FATX_FAT_RESERVED_ENTRIES_COUNT;
    fs->fat_type = fat_entries < 0xFFF0ULL ? XEMU_FATX_FAT_TYPE_16 : XEMU_FATX_FAT_TYPE_32;
    fs->fat_size = fat_entries * (fs->fat_type == XEMU_FATX_FAT_TYPE_16 ? 2ULL : 4ULL);
    if (fs->fat_size % XEMU_FATX_SUPERBLOCK_SIZE != 0) {
        fs->fat_size += XEMU_FATX_SUPERBLOCK_SIZE -
                        (fs->fat_size % XEMU_FATX_SUPERBLOCK_SIZE);
    }

    fs->volume_id = superblock.volume_id;
    fs->sectors_per_cluster = superblock.sectors_per_cluster;
    fs->root_cluster = superblock.root_cluster;
    fs->bytes_per_cluster = bytes_per_cluster;
    fs->fat_entry_count = (uint32_t)fat_entries;
    fs->fat_offset = partition_offset + XEMU_FATX_FAT_OFFSET;
    fs->cluster_offset = fs->fat_offset + fs->fat_size;

    if (fs->root_cluster < XEMU_FATX_FAT_RESERVED_ENTRIES_COUNT ||
        fs->root_cluster >= fs->fat_entry_count) {
        error_setg(errp, "Partition at 0x%" PRIx64 " has an invalid FATX root cluster",
                   partition_offset);
        return false;
    }

    return true;
}

static void xemu_fatx_close_fs(XemuFatxFs *fs)
{
    if (fs->fat_cache.data && fs->fat_cache.dirty) {
        xemu_fatx_dev_seek(fs, fs->fat_offset +
                               (uint64_t)fs->fat_cache.position * fs->fat_cache.entry_size);
        xemu_fatx_dev_write(fs, fs->fat_cache.data,
                            fs->fat_cache.entry_size, fs->fat_cache.entries);
    }
    g_free(fs->fat_cache.data);
    g_free(fs->error_message);
    memset(fs, 0, sizeof(*fs));
}

static bool xemu_fatx_populate_fat_cache(XemuFatxFs *fs, uint32_t index)
{
    XemuFatxCache *cache = &fs->fat_cache;

    if (index >= fs->fat_entry_count) {
        xemu_fatx_set_error(fs, "FAT index %u is out of range", index);
        return false;
    }

    if (cache->data && cache->dirty) {
        xemu_fatx_dev_seek(fs, fs->fat_offset +
                               (uint64_t)cache->position * cache->entry_size);
        if (xemu_fatx_dev_write(fs, cache->data, cache->entry_size, cache->entries) != cache->entries) {
            if (!fs->error_message) {
                xemu_fatx_set_error(fs, "Failed to flush the FAT cache");
            }
            return false;
        }
        cache->dirty = false;
    }

    cache->position = index;
    cache->entries = MIN(XEMU_FATX_FAT_CACHE_NUM_ENTRIES, fs->fat_entry_count - index);
    cache->entry_size = fs->fat_type == XEMU_FATX_FAT_TYPE_16 ? 2 : 4;
    size_t needed = (size_t)cache->entries * cache->entry_size;
    if (needed > cache->data_capacity) {
        g_free(cache->data);
        cache->data = g_malloc(needed);
        cache->data_capacity = needed;
    }

    xemu_fatx_dev_seek(fs, fs->fat_offset +
                           (uint64_t)cache->position * cache->entry_size);
    if (xemu_fatx_dev_read(fs, cache->data, cache->entry_size, cache->entries) != cache->entries) {
        if (!fs->error_message) {
            xemu_fatx_set_error(fs, "Failed to populate the FAT cache");
        }
        return false;
    }

    return true;
}

static bool xemu_fatx_read_fat(XemuFatxFs *fs, uint32_t index, uint32_t *entry)
{
    XemuFatxCache *cache = &fs->fat_cache;

    if (index >= fs->fat_entry_count) {
        xemu_fatx_set_error(fs, "FAT index %u is out of range", index);
        return false;
    }

    if (!cache->data ||
        index < cache->position ||
        index >= cache->position + cache->entries) {
        if (!xemu_fatx_populate_fat_cache(fs, index)) {
            return false;
        }
    }

    if (fs->fat_type == XEMU_FATX_FAT_TYPE_16) {
        *entry = ((uint16_t *)cache->data)[index - cache->position];
    } else {
        *entry = ((uint32_t *)cache->data)[index - cache->position];
    }

    return true;
}

static bool xemu_fatx_write_fat(XemuFatxFs *fs, uint32_t index, uint32_t entry)
{
    XemuFatxCache *cache = &fs->fat_cache;

    if (index >= fs->fat_entry_count) {
        xemu_fatx_set_error(fs, "FAT index %u is out of range", index);
        return false;
    }

    if (!cache->data ||
        index < cache->position ||
        index >= cache->position + cache->entries) {
        if (!xemu_fatx_populate_fat_cache(fs, index)) {
            return false;
        }
    }

    if (fs->fat_type == XEMU_FATX_FAT_TYPE_16) {
        ((uint16_t *)cache->data)[index - cache->position] = (uint16_t)entry;
    } else {
        ((uint32_t *)cache->data)[index - cache->position] = entry;
    }
    cache->dirty = true;
    return true;
}

static int xemu_fatx_get_fat_entry_type(XemuFatxFs *fs, uint32_t entry)
{
    if (fs->fat_type == XEMU_FATX_FAT_TYPE_16) {
        entry = (uint32_t)(int32_t)(int16_t)(uint16_t)entry;
    }

    switch (entry) {
    case 0x00000000:
        return XEMU_FATX_CLUSTER_AVAILABLE;
    case 0xFFFFFFF0:
        return XEMU_FATX_CLUSTER_RESERVED;
    case 0xFFFFFFF7:
        return XEMU_FATX_CLUSTER_BAD;
    case 0xFFFFFFF8:
        return XEMU_FATX_CLUSTER_MEDIA;
    case 0xFFFFFFFF:
    case 0x0000FFFF:
        return XEMU_FATX_CLUSTER_END;
    default:
        break;
    }

    if (entry < 0xFFFFFFF0U) {
        return XEMU_FATX_CLUSTER_DATA;
    }
    return XEMU_FATX_CLUSTER_INVALID;
}

static bool xemu_fatx_mark_cluster_end(XemuFatxFs *fs, uint32_t cluster)
{
    return xemu_fatx_write_fat(fs, cluster,
                               fs->fat_type == XEMU_FATX_FAT_TYPE_16 ? 0xFFFFU : 0xFFFFFFFFU);
}

static bool xemu_fatx_mark_cluster_available(XemuFatxFs *fs, uint32_t cluster)
{
    return xemu_fatx_write_fat(fs, cluster, 0);
}

static bool xemu_fatx_get_next_cluster(XemuFatxFs *fs, uint32_t *cluster)
{
    uint32_t entry;

    if (!xemu_fatx_read_fat(fs, *cluster, &entry)) {
        return false;
    }
    if (xemu_fatx_get_fat_entry_type(fs, entry) != XEMU_FATX_CLUSTER_DATA) {
        xemu_fatx_set_error(fs, "Expected another cluster in the chain");
        return false;
    }

    *cluster = entry;
    return true;
}

static bool xemu_fatx_alloc_cluster(XemuFatxFs *fs, uint32_t *cluster, bool zero_fill)
{
    uint32_t start = MAX(fs->next_alloc_cluster, 2U);
    uint32_t current = start;
    bool wrapped = false;

    for (;;) {
        uint32_t entry;
        uint8_t *zeroes;

        if (current >= fs->fat_entry_count) {
            current = 2;
            wrapped = true;
        }
        if (current == start && wrapped) {
            xemu_fatx_set_error(fs, "No free FATX clusters are available");
            return false;
        }
        if (!xemu_fatx_read_fat(fs, current, &entry)) {
            return false;
        }
        if (xemu_fatx_get_fat_entry_type(fs, entry) != XEMU_FATX_CLUSTER_AVAILABLE) {
            current++;
            continue;
        }

        if (!xemu_fatx_mark_cluster_end(fs, current)) {
            return false;
        }
        if (zero_fill) {
            zeroes = g_malloc0(fs->bytes_per_cluster);
            if (!xemu_fatx_dev_seek_cluster(fs, current, 0) ||
                xemu_fatx_dev_write(fs, zeroes, fs->bytes_per_cluster, 1) != 1) {
                g_free(zeroes);
                if (!fs->error_message) {
                    xemu_fatx_set_error(fs, "Failed to zero a new FATX cluster");
                }
                return false;
            }
            g_free(zeroes);
        }

        fs->next_alloc_cluster = current + 1;
        *cluster = current;
        return true;
    }
}

static bool xemu_fatx_attach_cluster(XemuFatxFs *fs, uint32_t tail, uint32_t cluster)
{
    uint32_t entry;

    if (!xemu_fatx_read_fat(fs, tail, &entry)) {
        return false;
    }
    if (xemu_fatx_get_fat_entry_type(fs, entry) != XEMU_FATX_CLUSTER_END) {
        xemu_fatx_set_error(fs, "Tried to append to a FATX cluster chain that is not terminated");
        return false;
    }
    if (!xemu_fatx_write_fat(fs, tail, cluster) ||
        !xemu_fatx_mark_cluster_end(fs, cluster)) {
        return false;
    }
    return true;
}

static bool xemu_fatx_free_cluster_chain(XemuFatxFs *fs, uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;

    while (cluster != 0) {
        uint32_t next = 0;
        uint32_t entry;
        int type;

        if (!xemu_fatx_read_fat(fs, cluster, &entry)) {
            return false;
        }
        type = xemu_fatx_get_fat_entry_type(fs, entry);
        if (type == XEMU_FATX_CLUSTER_DATA) {
            next = entry;
        }
        if (!xemu_fatx_mark_cluster_available(fs, cluster)) {
            return false;
        }
        if (type != XEMU_FATX_CLUSTER_DATA) {
            break;
        }
        cluster = next;
    }

    return true;
}

static char *xemu_fatx_dirname(const char *path)
{
    const char *slash;

    if (!path || path[0] == '\0') {
        return g_strdup("/");
    }
    slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return g_strdup("/");
    }
    return g_strndup(path, slash - path);
}

static char *xemu_fatx_basename_dup(const char *path)
{
    const char *slash = strrchr(path, '/');

    return g_strdup(slash ? slash + 1 : path);
}

static bool xemu_fatx_get_path_component(const char *path,
                                         size_t component,
                                         const char **start,
                                         size_t *len)
{
    size_t i = 0;

    *start = NULL;
    *len = 0;

    while (path[i] != '\0') {
        size_t j = i;

        while (path[j] != '\0' && path[j] != '/') {
            j++;
        }
        if (path[j] == '/') {
            j++;
        }
        if (component == 0) {
            *start = path + i;
            *len = j - i;
            return true;
        }
        if (path[j] == '\0') {
            return true;
        }
        i = j;
        component--;
    }

    return true;
}

static char *xemu_fatx_join_path(const char *dir, const char *name)
{
    if (g_strcmp0(dir, "/") == 0) {
        return g_strdup_printf("/%s", name);
    }
    return g_strdup_printf("%s/%s", dir, name);
}

static void xemu_fatx_time_to_ts(time_t value, XemuFatxTs *out)
{
    struct tm time_parts;
    struct tm *resolved;

    memset(out, 0, sizeof(*out));
    resolved = localtime_r(&value, &time_parts);
    if (!resolved) {
        return;
    }
    out->second = (uint8_t)time_parts.tm_sec;
    out->minute = (uint8_t)time_parts.tm_min;
    out->hour = (uint8_t)time_parts.tm_hour;
    out->day = (uint8_t)time_parts.tm_mday;
    out->month = (uint8_t)(time_parts.tm_mon + 1);
    out->year = (uint16_t)(time_parts.tm_year + 1900);
}

static uint16_t xemu_fatx_pack_date(const XemuFatxTs *value)
{
    uint16_t year = value->year > 2000 ? (uint16_t)(value->year - 2000) : 0;

    return (uint16_t)((value->day & 0x1F) |
                      ((value->month & 0x0F) << 5) |
                      ((year & 0x7F) << 9));
}

static uint16_t xemu_fatx_pack_time(const XemuFatxTs *value)
{
    return (uint16_t)(((value->hour & 0x1F) << 11) |
                      ((value->minute & 0x3F) << 5) |
                      ((value->second / 2) & 0x1F));
}

static void xemu_fatx_raw_to_attr(const XemuFatxRawDirectoryEntry *entry, XemuFatxAttr *attr)
{
    memset(attr, 0, sizeof(*attr));
    memcpy(attr->filename, entry->filename, entry->filename_len);
    attr->filename[entry->filename_len] = '\0';
    attr->attributes = entry->attributes & XEMU_FATX_ATTR_MASK;
    attr->first_cluster = entry->first_cluster;
    attr->file_size = entry->file_size;
}

static void xemu_fatx_attr_to_raw(const XemuFatxAttr *attr, XemuFatxRawDirectoryEntry *entry)
{
    size_t filename_len = strlen(attr->filename);

    memset(entry, 0, sizeof(*entry));
    entry->filename_len = (uint8_t)filename_len;
    entry->attributes = attr->attributes & XEMU_FATX_ATTR_MASK;
    memcpy(entry->filename, attr->filename, filename_len);
    entry->first_cluster = attr->first_cluster;
    entry->file_size = attr->file_size;
    entry->modified_date = xemu_fatx_pack_date(&attr->modified);
    entry->modified_time = xemu_fatx_pack_time(&attr->modified);
    entry->created_date = xemu_fatx_pack_date(&attr->created);
    entry->created_time = xemu_fatx_pack_time(&attr->created);
    entry->accessed_date = xemu_fatx_pack_date(&attr->accessed);
    entry->accessed_time = xemu_fatx_pack_time(&attr->accessed);
}

static int xemu_fatx_read_dir(XemuFatxFs *fs,
                              XemuFatxDir *dir,
                              XemuFatxDirent *entry,
                              XemuFatxAttr *attr)
{
    XemuFatxRawDirectoryEntry raw_entry;
    uint64_t offset = (uint64_t)dir->entry * sizeof(raw_entry);

    if (!xemu_fatx_dev_seek_cluster(fs, dir->cluster, offset) ||
        xemu_fatx_dev_read(fs, &raw_entry, sizeof(raw_entry), 1) != 1) {
        if (!fs->error_message) {
            xemu_fatx_set_error(fs, "Failed to read a FATX directory entry");
        }
        return XEMU_FATX_STATUS_ERROR;
    }

    if (raw_entry.filename_len == XEMU_FATX_END_OF_DIR_MARKER ||
        raw_entry.filename_len == XEMU_FATX_END_OF_DIR_MARKER2) {
        return XEMU_FATX_STATUS_END_OF_DIR;
    }
    if (raw_entry.filename_len == XEMU_FATX_DELETED_FILE_MARKER) {
        return XEMU_FATX_STATUS_FILE_DELETED;
    }
    if (raw_entry.filename_len > XEMU_FATX_MAX_FILENAME_LEN) {
        xemu_fatx_set_error(fs, "Encountered an invalid FATX filename length");
        return XEMU_FATX_STATUS_ERROR;
    }

    memset(entry, 0, sizeof(*entry));
    memcpy(entry->filename, raw_entry.filename, raw_entry.filename_len);
    entry->filename[raw_entry.filename_len] = '\0';
    if (attr) {
        xemu_fatx_raw_to_attr(&raw_entry, attr);
    }

    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_next_dir_entry(XemuFatxFs *fs, XemuFatxDir *dir)
{
    uint32_t entry;

    dir->entry += 1;
    if ((uint64_t)dir->entry <
        fs->bytes_per_cluster / sizeof(XemuFatxRawDirectoryEntry)) {
        return XEMU_FATX_STATUS_SUCCESS;
    }

    if (!xemu_fatx_read_fat(fs, dir->cluster, &entry)) {
        return XEMU_FATX_STATUS_ERROR;
    }
    if (xemu_fatx_get_fat_entry_type(fs, entry) != XEMU_FATX_CLUSTER_DATA) {
        xemu_fatx_set_error(fs, "Expected another FATX directory cluster");
        return XEMU_FATX_STATUS_ERROR;
    }

    dir->cluster = entry;
    dir->entry = 0;
    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_open_dir(XemuFatxFs *fs, const char *path, XemuFatxDir *dir)
{
    const char *start;
    size_t len;
    size_t component;
    int status;

    if (!xemu_fatx_get_path_component(path, 0, &start, &len) ||
        !start || len != 1 || start[0] != '/') {
        xemu_fatx_set_error(fs, "Invalid FATX path: %s", path);
        return XEMU_FATX_STATUS_ERROR;
    }

    dir->cluster = fs->root_cluster;
    dir->entry = 0;

    for (component = 1;; component++) {
        XemuFatxDirent dirent;
        XemuFatxAttr attr;

        if (!xemu_fatx_get_path_component(path, component, &start, &len)) {
            xemu_fatx_set_error(fs, "Invalid FATX path");
            return XEMU_FATX_STATUS_ERROR;
        }
        if (!start) {
            return XEMU_FATX_STATUS_SUCCESS;
        }

        if (len > 0 && start[len - 1] == '/') {
            len--;
        }

        while (1) {
            status = xemu_fatx_read_dir(fs, dir, &dirent, &attr);
            if (status == XEMU_FATX_STATUS_SUCCESS) {
                if ((attr.attributes & XEMU_FATX_ATTR_DIRECTORY) != 0 &&
                    strlen(dirent.filename) == len &&
                    xemu_fatx_ascii_equal_n(dirent.filename, start, len)) {
                    dir->cluster = attr.first_cluster;
                    dir->entry = 0;
                    break;
                }
            } else if (status == XEMU_FATX_STATUS_FILE_DELETED) {
                /* Skip. */
            } else if (status == XEMU_FATX_STATUS_END_OF_DIR) {
                return XEMU_FATX_STATUS_FILE_NOT_FOUND;
            } else {
                return XEMU_FATX_STATUS_ERROR;
            }

            status = xemu_fatx_next_dir_entry(fs, dir);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                return status;
            }
        }
    }
}

static int xemu_fatx_get_attr_dir(XemuFatxFs *fs,
                                  const char *basename,
                                  XemuFatxDir *dir,
                                  XemuFatxDirent *dirent,
                                  XemuFatxAttr *attr)
{
    int status;

    while (1) {
        status = xemu_fatx_read_dir(fs, dir, dirent, attr);
        if (status == XEMU_FATX_STATUS_SUCCESS) {
            if (xemu_fatx_ascii_equal(basename, dirent->filename)) {
                return XEMU_FATX_STATUS_SUCCESS;
            }
        } else if (status == XEMU_FATX_STATUS_FILE_DELETED) {
            /* Skip. */
        } else if (status == XEMU_FATX_STATUS_END_OF_DIR) {
            return XEMU_FATX_STATUS_FILE_NOT_FOUND;
        } else {
            return XEMU_FATX_STATUS_ERROR;
        }

        status = xemu_fatx_next_dir_entry(fs, dir);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
    }
}

static int xemu_fatx_get_attr(XemuFatxFs *fs, const char *path, XemuFatxAttr *attr)
{
    g_autofree char *dir_path = xemu_fatx_dirname(path);
    g_autofree char *basename = xemu_fatx_basename_dup(path);
    XemuFatxDir dir;
    XemuFatxDirent dirent;
    int status = xemu_fatx_open_dir(fs, dir_path, &dir);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    return xemu_fatx_get_attr_dir(fs, basename, &dir, &dirent, attr);
}

static int xemu_fatx_write_dir(XemuFatxFs *fs,
                               XemuFatxDir *dir,
                               const XemuFatxDirent *dirent,
                               const XemuFatxAttr *attr)
{
    XemuFatxRawDirectoryEntry raw_entry;
    uint64_t offset = (uint64_t)dir->entry * sizeof(raw_entry);

    if (!xemu_fatx_dev_seek_cluster(fs, dir->cluster, offset)) {
        return XEMU_FATX_STATUS_ERROR;
    }

    xemu_fatx_attr_to_raw(attr, &raw_entry);
    memcpy(raw_entry.filename, dirent->filename, raw_entry.filename_len);

    if (xemu_fatx_dev_write(fs, &raw_entry, sizeof(raw_entry), 1) != 1) {
        if (!fs->error_message) {
            xemu_fatx_set_error(fs, "Failed to write a FATX directory entry");
        }
        return XEMU_FATX_STATUS_ERROR;
    }

    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_set_attr(XemuFatxFs *fs, const char *path, const XemuFatxAttr *new_attr)
{
    g_autofree char *dir_path = xemu_fatx_dirname(path);
    g_autofree char *basename = xemu_fatx_basename_dup(path);
    XemuFatxDir dir;
    XemuFatxDirent dirent;
    XemuFatxAttr old_attr;
    int status = xemu_fatx_open_dir(fs, dir_path, &dir);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }
    status = xemu_fatx_get_attr_dir(fs, basename, &dir, &dirent, &old_attr);
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    return xemu_fatx_write_dir(fs, &dir, &dirent, new_attr);
}

static int xemu_fatx_mark_dir_entry(XemuFatxFs *fs, XemuFatxDir *dir, uint8_t marker)
{
    XemuFatxRawDirectoryEntry raw_entry;
    uint64_t offset = (uint64_t)dir->entry * sizeof(raw_entry);

    memset(&raw_entry, 0, sizeof(raw_entry));
    raw_entry.filename_len = marker;

    if (!xemu_fatx_dev_seek_cluster(fs, dir->cluster, offset) ||
        xemu_fatx_dev_write(fs, &raw_entry, sizeof(raw_entry), 1) != 1) {
        if (!fs->error_message) {
            xemu_fatx_set_error(fs, "Failed to update a FATX directory marker");
        }
        return XEMU_FATX_STATUS_ERROR;
    }

    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_alloc_dir_entry(XemuFatxFs *fs, XemuFatxDir *dir)
{
    XemuFatxDirent entry;
    XemuFatxAttr attr;
    int status;

    dir->entry = 0;
    while (1) {
        status = xemu_fatx_read_dir(fs, dir, &entry, &attr);
        if (status == XEMU_FATX_STATUS_SUCCESS) {
            status = xemu_fatx_next_dir_entry(fs, dir);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                break;
            }
        } else if (status == XEMU_FATX_STATUS_FILE_DELETED) {
            return XEMU_FATX_STATUS_SUCCESS;
        } else if (status == XEMU_FATX_STATUS_END_OF_DIR) {
            break;
        } else {
            return XEMU_FATX_STATUS_ERROR;
        }
    }

    if ((uint64_t)dir->entry <
        fs->bytes_per_cluster / sizeof(XemuFatxRawDirectoryEntry)) {
        dir->entry += 1;
        status = xemu_fatx_mark_dir_entry(fs, dir, XEMU_FATX_END_OF_DIR_MARKER);
        dir->entry -= 1;
        return status;
    }

    {
        uint32_t new_cluster;
        uint32_t current_cluster = dir->cluster;
        uint32_t current_entry = dir->entry;

        if (!xemu_fatx_alloc_cluster(fs, &new_cluster, true) ||
            !xemu_fatx_attach_cluster(fs, current_cluster, new_cluster)) {
            return XEMU_FATX_STATUS_ERROR;
        }

        dir->cluster = new_cluster;
        dir->entry = 0;
        status = xemu_fatx_mark_dir_entry(fs, dir, XEMU_FATX_END_OF_DIR_MARKER);
        dir->cluster = current_cluster;
        dir->entry = current_entry;
        return status;
    }
}

static int xemu_fatx_create_dirent(XemuFatxFs *fs,
                                   const char *path,
                                   XemuFatxDir *dir,
                                   uint8_t attributes)
{
    g_autofree char *basename = xemu_fatx_basename_dup(path);
    XemuFatxDirent dirent;
    XemuFatxAttr attr;
    uint32_t cluster;
    time_t now;

    if (strlen(basename) > XEMU_FATX_MAX_FILENAME_LEN) {
        xemu_fatx_set_error(fs, "The FATX filename \"%s\" is too long", basename);
        return XEMU_FATX_STATUS_ERROR;
    }
    if (!xemu_fatx_alloc_cluster(fs, &cluster, true)) {
        return XEMU_FATX_STATUS_ERROR;
    }
    if (xemu_fatx_alloc_dir_entry(fs, dir) != XEMU_FATX_STATUS_SUCCESS) {
        xemu_fatx_free_cluster_chain(fs, cluster);
        return XEMU_FATX_STATUS_ERROR;
    }

    memset(&dirent, 0, sizeof(dirent));
    g_strlcpy(dirent.filename, basename, sizeof(dirent.filename));

    memset(&attr, 0, sizeof(attr));
    g_strlcpy(attr.filename, basename, sizeof(attr.filename));
    attr.attributes = attributes;
    attr.first_cluster = cluster;
    attr.file_size = 0;
    now = time(NULL);
    xemu_fatx_time_to_ts(now, &attr.created);
    xemu_fatx_time_to_ts(now, &attr.modified);
    xemu_fatx_time_to_ts(now, &attr.accessed);

    if (xemu_fatx_write_dir(fs, dir, &dirent, &attr) != XEMU_FATX_STATUS_SUCCESS) {
        xemu_fatx_free_cluster_chain(fs, cluster);
        return XEMU_FATX_STATUS_ERROR;
    }

    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_mknod(XemuFatxFs *fs, const char *path)
{
    g_autofree char *dir_path = xemu_fatx_dirname(path);
    XemuFatxDir dir;
    XemuFatxAttr attr;
    int status = xemu_fatx_get_attr(fs, path, &attr);

    if (status == XEMU_FATX_STATUS_SUCCESS) {
        xemu_fatx_set_error(fs, "FATX path already exists: %s", path);
        return XEMU_FATX_STATUS_ERROR;
    }

    status = xemu_fatx_open_dir(fs, dir_path, &dir);
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    return xemu_fatx_create_dirent(fs, path, &dir, 0);
}

static int xemu_fatx_mkdir(XemuFatxFs *fs, const char *path)
{
    g_autofree char *dir_path = xemu_fatx_dirname(path);
    XemuFatxDir dir;
    XemuFatxAttr attr;
    int status = xemu_fatx_get_attr(fs, path, &attr);

    if (status == XEMU_FATX_STATUS_SUCCESS) {
        if ((attr.attributes & XEMU_FATX_ATTR_DIRECTORY) != 0) {
            return XEMU_FATX_STATUS_SUCCESS;
        }
        xemu_fatx_set_error(fs, "A file already exists at %s", path);
        return XEMU_FATX_STATUS_ERROR;
    }

    status = xemu_fatx_open_dir(fs, dir_path, &dir);
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }
    status = xemu_fatx_create_dirent(fs, path, &dir, XEMU_FATX_ATTR_DIRECTORY);
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }
    status = xemu_fatx_open_dir(fs, path, &dir);
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }
    return xemu_fatx_mark_dir_entry(fs, &dir, XEMU_FATX_END_OF_DIR_MARKER);
}

static int xemu_fatx_ensure_dir(XemuFatxFs *fs, const char *path)
{
    XemuFatxAttr attr;
    int status;

    if (g_strcmp0(path, "/") == 0) {
        return XEMU_FATX_STATUS_SUCCESS;
    }

    status = xemu_fatx_get_attr(fs, path, &attr);
    if (status == XEMU_FATX_STATUS_SUCCESS) {
        if ((attr.attributes & XEMU_FATX_ATTR_DIRECTORY) != 0) {
            return XEMU_FATX_STATUS_SUCCESS;
        }
        xemu_fatx_set_error(fs, "A file already exists where a directory is needed: %s", path);
        return XEMU_FATX_STATUS_ERROR;
    }

    {
        g_autofree char *parent = xemu_fatx_dirname(path);

        status = xemu_fatx_ensure_dir(fs, parent);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
    }

    return xemu_fatx_mkdir(fs, path);
}

static bool xemu_fatx_find_cluster_for_offset(XemuFatxFs *fs,
                                              XemuFatxAttr *attr,
                                              uint32_t offset,
                                              uint32_t *result,
                                              bool alloc)
{
    uint32_t cluster = attr->first_cluster;

    if (cluster == 0) {
        xemu_fatx_set_error(fs, "Encountered a FATX file with no cluster chain");
        return false;
    }

    while (offset >= fs->bytes_per_cluster) {
        uint32_t entry;
        int type;

        if (!xemu_fatx_read_fat(fs, cluster, &entry)) {
            return false;
        }
        type = xemu_fatx_get_fat_entry_type(fs, entry);
        if (type == XEMU_FATX_CLUSTER_DATA) {
            cluster = entry;
        } else if (alloc && type == XEMU_FATX_CLUSTER_END) {
            uint32_t new_cluster;

            if (!xemu_fatx_alloc_cluster(fs, &new_cluster, true) ||
                !xemu_fatx_attach_cluster(fs, cluster, new_cluster)) {
                return false;
            }
            cluster = new_cluster;
        } else {
            xemu_fatx_set_error(fs, "FATX cluster chain ended unexpectedly");
            return false;
        }
        offset -= fs->bytes_per_cluster;
    }

    *result = cluster;
    return true;
}

static int xemu_fatx_write_file(XemuFatxFs *fs,
                                const char *path,
                                const uint8_t *buffer,
                                size_t size)
{
    XemuFatxAttr attr;
    uint32_t cluster;
    uint64_t cluster_offset;
    size_t total_written = 0;
    int status = xemu_fatx_get_attr(fs, path, &attr);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    if (!xemu_fatx_find_cluster_for_offset(fs, &attr, 0, &cluster, true)) {
        return XEMU_FATX_STATUS_ERROR;
    }

    cluster_offset = 0;
    while (total_written < size) {
        size_t bytes_to_write = MIN(fs->bytes_per_cluster - cluster_offset,
                                    size - total_written);

        if (!xemu_fatx_dev_seek_cluster(fs, cluster, cluster_offset) ||
            xemu_fatx_dev_write(fs, buffer + total_written, 1, bytes_to_write) != bytes_to_write) {
            if (!fs->error_message) {
                xemu_fatx_set_error(fs, "Failed to write FATX file contents");
            }
            return XEMU_FATX_STATUS_ERROR;
        }

        total_written += bytes_to_write;
        cluster_offset += bytes_to_write;
        if (total_written == size) {
            break;
        }
        if (cluster_offset >= fs->bytes_per_cluster) {
            uint32_t entry;
            int type;

            if (!xemu_fatx_read_fat(fs, cluster, &entry)) {
                return XEMU_FATX_STATUS_ERROR;
            }
            type = xemu_fatx_get_fat_entry_type(fs, entry);
            if (type != XEMU_FATX_CLUSTER_DATA) {
                uint32_t new_cluster;

                if (!xemu_fatx_alloc_cluster(fs, &new_cluster,
                                             size - total_written < fs->bytes_per_cluster) ||
                    !xemu_fatx_attach_cluster(fs, cluster, new_cluster)) {
                    return XEMU_FATX_STATUS_ERROR;
                }
                cluster = new_cluster;
            } else {
                cluster = entry;
            }
            cluster_offset = 0;
        }
    }

    if (attr.file_size != size) {
        time_t now = time(NULL);

        attr.file_size = (uint32_t)size;
        xemu_fatx_time_to_ts(now, &attr.modified);
        xemu_fatx_time_to_ts(now, &attr.accessed);
        return xemu_fatx_set_attr(fs, path, &attr);
    }

    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_read_file(XemuFatxFs *fs,
                               const char *path,
                               uint8_t **buffer_out,
                               size_t *size_out)
{
    XemuFatxAttr attr;
    uint8_t *buffer = NULL;
    size_t total_read = 0;
    uint32_t cluster;
    uint64_t cluster_offset = 0;
    int status = xemu_fatx_get_attr(fs, path, &attr);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }
    if ((attr.attributes & XEMU_FATX_ATTR_DIRECTORY) != 0) {
        xemu_fatx_set_error(fs, "%s is a directory, not a file", path);
        return XEMU_FATX_STATUS_ERROR;
    }

    buffer = g_malloc(attr.file_size ? attr.file_size : 1);
    if (attr.file_size == 0) {
        *buffer_out = buffer;
        *size_out = 0;
        return XEMU_FATX_STATUS_SUCCESS;
    }

    if (!xemu_fatx_find_cluster_for_offset(fs, &attr, 0, &cluster, false)) {
        g_free(buffer);
        return XEMU_FATX_STATUS_ERROR;
    }

    while (total_read < attr.file_size) {
        size_t bytes_to_read = MIN(fs->bytes_per_cluster - cluster_offset,
                                   attr.file_size - total_read);

        if (!xemu_fatx_dev_seek_cluster(fs, cluster, cluster_offset) ||
            xemu_fatx_dev_read(fs, buffer + total_read, 1, bytes_to_read) != bytes_to_read) {
            g_free(buffer);
            if (!fs->error_message) {
                xemu_fatx_set_error(fs, "Failed to read FATX file contents");
            }
            return XEMU_FATX_STATUS_ERROR;
        }

        total_read += bytes_to_read;
        cluster_offset += bytes_to_read;
        if (total_read == attr.file_size) {
            break;
        }
        if (cluster_offset >= fs->bytes_per_cluster) {
            if (!xemu_fatx_get_next_cluster(fs, &cluster)) {
                g_free(buffer);
                return XEMU_FATX_STATUS_ERROR;
            }
            cluster_offset = 0;
        }
    }

    *buffer_out = buffer;
    *size_out = attr.file_size;
    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_unlink(XemuFatxFs *fs, const char *path)
{
    g_autofree char *dir_path = xemu_fatx_dirname(path);
    g_autofree char *basename = xemu_fatx_basename_dup(path);
    XemuFatxDir dir;
    XemuFatxDirent entry;
    XemuFatxAttr attr;
    int status = xemu_fatx_open_dir(fs, dir_path, &dir);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    while (1) {
        status = xemu_fatx_read_dir(fs, &dir, &entry, &attr);
        if (status == XEMU_FATX_STATUS_SUCCESS) {
            if (xemu_fatx_ascii_equal(entry.filename, basename)) {
                break;
            }
        } else if (status == XEMU_FATX_STATUS_FILE_DELETED) {
            /* Skip. */
        } else if (status == XEMU_FATX_STATUS_END_OF_DIR) {
            return XEMU_FATX_STATUS_FILE_NOT_FOUND;
        } else {
            return status;
        }

        status = xemu_fatx_next_dir_entry(fs, &dir);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
    }

    if (!xemu_fatx_free_cluster_chain(fs, attr.first_cluster)) {
        return XEMU_FATX_STATUS_ERROR;
    }

    return xemu_fatx_mark_dir_entry(fs, &dir, XEMU_FATX_DELETED_FILE_MARKER);
}

static int xemu_fatx_rmdir(XemuFatxFs *fs, const char *path)
{
    XemuFatxDir dir;
    XemuFatxDirent dirent;
    XemuFatxAttr attr;
    int status = xemu_fatx_open_dir(fs, path, &dir);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    while (1) {
        status = xemu_fatx_read_dir(fs, &dir, &dirent, &attr);
        if (status == XEMU_FATX_STATUS_SUCCESS) {
            xemu_fatx_set_error(fs, "Directory %s is not empty", path);
            return XEMU_FATX_STATUS_ERROR;
        }
        if (status == XEMU_FATX_STATUS_FILE_DELETED) {
            status = xemu_fatx_next_dir_entry(fs, &dir);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                return status;
            }
            continue;
        }
        if (status == XEMU_FATX_STATUS_END_OF_DIR) {
            break;
        }
        return status;
    }

    return xemu_fatx_unlink(fs, path);
}

static int xemu_fatx_remove_tree(XemuFatxFs *fs, const char *path)
{
    XemuFatxAttr attr;
    int status = xemu_fatx_get_attr(fs, path, &attr);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    if ((attr.attributes & XEMU_FATX_ATTR_DIRECTORY) == 0) {
        return xemu_fatx_unlink(fs, path);
    }

    {
        XemuFatxDir dir;
        XemuFatxDirent dirent;
        XemuFatxAttr child_attr;

        status = xemu_fatx_open_dir(fs, path, &dir);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
        while (1) {
            g_autofree char *child_path = NULL;

            status = xemu_fatx_read_dir(fs, &dir, &dirent, &child_attr);
            if (status == XEMU_FATX_STATUS_SUCCESS) {
                child_path = xemu_fatx_join_path(path, dirent.filename);
                status = xemu_fatx_remove_tree(fs, child_path);
                if (status != XEMU_FATX_STATUS_SUCCESS) {
                    return status;
                }
            } else if (status == XEMU_FATX_STATUS_FILE_DELETED) {
                /* Skip. */
            } else if (status == XEMU_FATX_STATUS_END_OF_DIR) {
                break;
            } else {
                return status;
            }

            status = xemu_fatx_next_dir_entry(fs, &dir);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                break;
            }
        }
        if (status != XEMU_FATX_STATUS_END_OF_DIR &&
            status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
    }

    return xemu_fatx_rmdir(fs, path);
}

static bool xemu_host_dir_has_entries(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    bool has_entries = false;

    if (!dir) {
        return false;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (g_strcmp0(entry->d_name, ".") == 0 ||
            g_strcmp0(entry->d_name, "..") == 0) {
            continue;
        }
        has_entries = true;
        break;
    }
    closedir(dir);
    return has_entries;
}

static bool xemu_should_skip_host_entry(const char *name)
{
    return name[0] == '\0' ||
           g_strcmp0(name, ".") == 0 ||
           g_strcmp0(name, "..") == 0 ||
           g_strcmp0(name, "__MACOSX") == 0 ||
           g_strcmp0(name, ".DS_Store") == 0;
}

static int xemu_fatx_export_tree(XemuFatxFs *fs,
                                 const char *fatx_path,
                                 const char *host_path)
{
    XemuFatxAttr attr;
    int status = xemu_fatx_get_attr(fs, fatx_path, &attr);

    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    if ((attr.attributes & XEMU_FATX_ATTR_DIRECTORY) != 0) {
        XemuFatxDir dir;
        XemuFatxDirent dirent;
        XemuFatxAttr child_attr;

        if (g_mkdir_with_parents(host_path, 0775) != 0) {
            xemu_fatx_set_error(fs, "Failed to create backup directory %s: %s",
                                host_path, g_strerror(errno));
            return XEMU_FATX_STATUS_ERROR;
        }
        status = xemu_fatx_open_dir(fs, fatx_path, &dir);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
        while (1) {
            g_autofree char *child_fatx_path = NULL;
            g_autofree char *child_host_path = NULL;

            status = xemu_fatx_read_dir(fs, &dir, &dirent, &child_attr);
            if (status == XEMU_FATX_STATUS_SUCCESS) {
                child_fatx_path = xemu_fatx_join_path(fatx_path, dirent.filename);
                child_host_path = g_build_filename(host_path, dirent.filename, NULL);
                status = xemu_fatx_export_tree(fs, child_fatx_path, child_host_path);
                if (status != XEMU_FATX_STATUS_SUCCESS) {
                    return status;
                }
            } else if (status == XEMU_FATX_STATUS_FILE_DELETED) {
                /* Skip. */
            } else if (status == XEMU_FATX_STATUS_END_OF_DIR) {
                return XEMU_FATX_STATUS_SUCCESS;
            } else {
                return status;
            }

            status = xemu_fatx_next_dir_entry(fs, &dir);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                return status;
            }
        }
    }

    {
        g_autofree char *parent = g_path_get_dirname(host_path);
        g_autofree uint8_t *buffer = NULL;
        size_t size = 0;
        FILE *file;

        if (g_mkdir_with_parents(parent, 0775) != 0) {
            xemu_fatx_set_error(fs, "Failed to create backup directory %s: %s",
                                parent, g_strerror(errno));
            return XEMU_FATX_STATUS_ERROR;
        }
        status = xemu_fatx_read_file(fs, fatx_path, &buffer, &size);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
        file = qemu_fopen(host_path, "wb");
        if (!file) {
            xemu_fatx_set_error(fs, "Failed to create backup file %s: %s",
                                host_path, g_strerror(errno));
            g_free(buffer);
            return XEMU_FATX_STATUS_ERROR;
        }
        if (size > 0 && fwrite(buffer, 1, size, file) != size) {
            xemu_fatx_set_error(fs, "Failed to write backup file %s", host_path);
            fclose(file);
            g_free(buffer);
            return XEMU_FATX_STATUS_ERROR;
        }
        fclose(file);
        g_free(buffer);
    }

    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_backup_conflicts(XemuFatxFs *fs,
                                      const char *source_dir,
                                      const char *backup_dir)
{
    DIR *dir = opendir(source_dir);
    struct dirent *entry;

    if (!dir) {
        xemu_fatx_set_error(fs, "Failed to open %s: %s", source_dir, g_strerror(errno));
        return XEMU_FATX_STATUS_ERROR;
    }

    while ((entry = readdir(dir)) != NULL) {
        XemuFatxAttr attr;
        g_autofree char *fatx_path = NULL;
        g_autofree char *host_path = NULL;
        int status;

        if (xemu_should_skip_host_entry(entry->d_name)) {
            continue;
        }

        fatx_path = xemu_fatx_join_path("/", entry->d_name);
        status = xemu_fatx_get_attr(fs, fatx_path, &attr);
        if (status == XEMU_FATX_STATUS_FILE_NOT_FOUND) {
            continue;
        }
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            closedir(dir);
            return status;
        }

        host_path = g_build_filename(backup_dir, entry->d_name, NULL);
        status = xemu_fatx_export_tree(fs, fatx_path, host_path);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            closedir(dir);
            return status;
        }
    }

    closedir(dir);
    return XEMU_FATX_STATUS_SUCCESS;
}

static int xemu_fatx_import_host_file(XemuFatxFs *fs,
                                      const char *host_path,
                                      const char *fatx_path)
{
    g_autofree char *parent = xemu_fatx_dirname(fatx_path);
    g_autofree char *contents = NULL;
    gsize length = 0;
    XemuFatxAttr attr;
    int status;

    status = xemu_fatx_ensure_dir(fs, parent);
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    status = xemu_fatx_get_attr(fs, fatx_path, &attr);
    if (status == XEMU_FATX_STATUS_SUCCESS) {
        status = xemu_fatx_remove_tree(fs, fatx_path);
        if (status != XEMU_FATX_STATUS_SUCCESS) {
            return status;
        }
    } else if (status != XEMU_FATX_STATUS_FILE_NOT_FOUND) {
        return status;
    }

    if (!g_file_get_contents(host_path, &contents, &length, NULL)) {
        xemu_fatx_set_error(fs, "Failed to read %s", host_path);
        return XEMU_FATX_STATUS_ERROR;
    }

    status = xemu_fatx_mknod(fs, fatx_path);
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        return status;
    }

    return xemu_fatx_write_file(fs, fatx_path, (const uint8_t *)contents, length);
}

static int xemu_fatx_import_host_tree(XemuFatxFs *fs,
                                      const char *host_dir,
                                      const char *fatx_dir)
{
    DIR *dir = opendir(host_dir);
    struct dirent *entry;

    if (!dir) {
        xemu_fatx_set_error(fs, "Failed to open %s: %s", host_dir, g_strerror(errno));
        return XEMU_FATX_STATUS_ERROR;
    }

    while ((entry = readdir(dir)) != NULL) {
        g_autofree char *host_path = NULL;
        g_autofree char *fatx_path = NULL;
        struct stat st;
        int status;

        if (xemu_should_skip_host_entry(entry->d_name)) {
            continue;
        }

        host_path = g_build_filename(host_dir, entry->d_name, NULL);
        fatx_path = xemu_fatx_join_path(fatx_dir, entry->d_name);

        if (stat(host_path, &st) != 0) {
            xemu_fatx_set_error(fs, "Failed to stat %s: %s", host_path, g_strerror(errno));
            closedir(dir);
            return XEMU_FATX_STATUS_ERROR;
        }

        if (S_ISDIR(st.st_mode)) {
            XemuFatxAttr attr;

            status = xemu_fatx_get_attr(fs, fatx_path, &attr);
            if (status == XEMU_FATX_STATUS_SUCCESS &&
                (attr.attributes & XEMU_FATX_ATTR_DIRECTORY) == 0) {
                status = xemu_fatx_remove_tree(fs, fatx_path);
                if (status != XEMU_FATX_STATUS_SUCCESS) {
                    closedir(dir);
                    return status;
                }
            } else if (status != XEMU_FATX_STATUS_SUCCESS &&
                       status != XEMU_FATX_STATUS_FILE_NOT_FOUND) {
                closedir(dir);
                return status;
            }

            status = xemu_fatx_ensure_dir(fs, fatx_path);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                closedir(dir);
                return status;
            }
            status = xemu_fatx_import_host_tree(fs, host_path, fatx_path);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                closedir(dir);
                return status;
            }
        } else if (S_ISREG(st.st_mode)) {
            status = xemu_fatx_import_host_file(fs, host_path, fatx_path);
            if (status != XEMU_FATX_STATUS_SUCCESS) {
                closedir(dir);
                return status;
            }
        }
    }

    closedir(dir);
    return XEMU_FATX_STATUS_SUCCESS;
}

static bool xemu_dashboard_import_partition(BlockBackend *blk,
                                            uint64_t offset,
                                            uint64_t size,
                                            const char *source_dir,
                                            const char *backup_dir,
                                            Error **errp)
{
    XemuFatxFs fs;
    int status;

    if (!xemu_fatx_open_fs(&fs, blk, offset, size, errp)) {
        return false;
    }

    status = xemu_fatx_backup_conflicts(&fs, source_dir, backup_dir);
    if (status == XEMU_FATX_STATUS_SUCCESS) {
        status = xemu_fatx_import_host_tree(&fs, source_dir, "/");
    }
    if (status != XEMU_FATX_STATUS_SUCCESS) {
        xemu_fatx_propagate_error(&fs, errp, NULL);
        xemu_fatx_close_fs(&fs);
        return false;
    }

    xemu_fatx_close_fs(&fs);
    return true;
}

static bool xemu_fatx_probe_path(XemuFatxFs *fs,
                                 const char *path,
                                 bool *exists_out,
                                 bool *is_dir_out)
{
    XemuFatxAttr attr;
    int status = xemu_fatx_get_attr(fs, path, &attr);

    if (status == XEMU_FATX_STATUS_SUCCESS) {
        if (exists_out) {
            *exists_out = true;
        }
        if (is_dir_out) {
            *is_dir_out = (attr.attributes & XEMU_FATX_ATTR_DIRECTORY) != 0;
        }
        return true;
    }

    if (status == XEMU_FATX_STATUS_FILE_NOT_FOUND) {
        if (exists_out) {
            *exists_out = false;
        }
        if (is_dir_out) {
            *is_dir_out = false;
        }
        return true;
    }

    return false;
}

static bool xemu_fatx_scan_root_dashboard_markers(XemuFatxFs *fs,
                                                  XemuFatxDashboardStatus *status,
                                                  Error **errp)
{
    XemuFatxDir dir;
    XemuFatxDirent dirent;
    XemuFatxAttr attr;
    int scan_status;

    if (!status) {
        return true;
    }

    scan_status = xemu_fatx_open_dir(fs, "/", &dir);
    if (scan_status != XEMU_FATX_STATUS_SUCCESS) {
        xemu_fatx_propagate_error(fs, errp, NULL);
        return false;
    }

    while (1) {
        scan_status = xemu_fatx_read_dir(fs, &dir, &dirent, &attr);
        if (scan_status == XEMU_FATX_STATUS_SUCCESS) {
            if ((attr.attributes & XEMU_FATX_ATTR_DIRECTORY) != 0) {
                if (xemu_fatx_ascii_equal(dirent.filename, "xodash")) {
                    status->xodash_dir_present = true;
                } else if (xemu_fatx_ascii_equal(dirent.filename, "audio")) {
                    status->audio_dir_present = true;
                } else if (xemu_fatx_ascii_equal(dirent.filename, "fonts")) {
                    status->fonts_dir_present = true;
                } else if (xemu_fatx_ascii_starts_with(dirent.filename, "xboxdashdata.")) {
                    status->xboxdashdata_dir_present = true;
                }
            } else {
                if (xemu_fatx_ascii_equal(dirent.filename, "xboxdash.xbe")) {
                    status->xboxdash_xbe_present = true;
                } else if (xemu_fatx_ascii_equal(dirent.filename, "msdash.xbe")) {
                    status->msdash_xbe_present = true;
                } else if (xemu_fatx_ascii_equal(dirent.filename, "xbox.xtf")) {
                    status->xbox_xtf_present = true;
                }
            }
        } else if (scan_status == XEMU_FATX_STATUS_FILE_DELETED) {
            /* Skip. */
        } else if (scan_status == XEMU_FATX_STATUS_END_OF_DIR) {
            return true;
        } else {
            xemu_fatx_propagate_error(fs, errp, NULL);
            return false;
        }

        scan_status = xemu_fatx_next_dir_entry(fs, &dir);
        if (scan_status != XEMU_FATX_STATUS_SUCCESS) {
            xemu_fatx_propagate_error(fs, errp, NULL);
            return false;
        }
    }
}

bool xemu_fatx_inspect_retail_dashboard(BlockBackend *blk,
                                        XemuFatxDashboardStatus *status,
                                        Error **errp)
{
    XemuFatxFs fs;
    bool ok = true;

    if (status) {
        memset(status, 0, sizeof(*status));
    }

    if (!xemu_fatx_open_fs(&fs, blk,
                           XEMU_XBOX_HDD_PARTITION_C_OFFSET,
                           XEMU_XBOX_HDD_PARTITION_C_SIZE,
                           errp)) {
        return false;
    }

    if (status) {
        bool is_dir = false;

        ok = xemu_fatx_probe_path(&fs, "/xboxdash.xbe",
                                  &status->xboxdash_xbe_present, NULL) &&
             xemu_fatx_probe_path(&fs, "/msdash.xbe",
                                  &status->msdash_xbe_present, NULL) &&
             xemu_fatx_probe_path(&fs, "/xbox.xtf",
                                  &status->xbox_xtf_present, NULL) &&
             xemu_fatx_probe_path(&fs, "/xodash",
                                  &status->xodash_dir_present, &is_dir);
        status->xodash_dir_present = status->xodash_dir_present && is_dir;
        ok = ok && xemu_fatx_scan_root_dashboard_markers(&fs, status, errp);
    }

    if (!ok) {
        xemu_fatx_propagate_error(&fs, errp, NULL);
        xemu_fatx_close_fs(&fs);
        return false;
    }

    xemu_fatx_close_fs(&fs);
    return true;
}

bool xemu_fatx_import_dashboard(BlockBackend *blk,
                                const char *source_root,
                                const char *backup_root,
                                Error **errp)
{
    g_autofree char *source_c = g_build_filename(source_root, "C", NULL);
    g_autofree char *source_e = g_build_filename(source_root, "E", NULL);
    g_autofree char *backup_c = g_build_filename(backup_root, "C", NULL);
    g_autofree char *backup_e = g_build_filename(backup_root, "E", NULL);
    bool has_c = g_file_test(source_c, G_FILE_TEST_IS_DIR) && xemu_host_dir_has_entries(source_c);
    bool has_e = g_file_test(source_e, G_FILE_TEST_IS_DIR) && xemu_host_dir_has_entries(source_e);
    bool imported_any = false;

    if (!has_c && !has_e && !xemu_host_dir_has_entries(source_root)) {
        error_setg(errp, "The selected dashboard source is empty.");
        return false;
    }

    if (has_c) {
        if (!xemu_dashboard_import_partition(
                blk,
                XEMU_XBOX_HDD_PARTITION_C_OFFSET,
                XEMU_XBOX_HDD_PARTITION_C_SIZE,
                source_c,
                backup_c,
                errp)) {
            return false;
        }
        imported_any = true;
    }

    if (has_e) {
        if (!xemu_dashboard_import_partition(
                blk,
                XEMU_XBOX_HDD_PARTITION_E_OFFSET,
                XEMU_XBOX_HDD_PARTITION_E_SIZE,
                source_e,
                backup_e,
                errp)) {
            return false;
        }
        imported_any = true;
    }

    if (!has_c && !has_e) {
        if (!xemu_dashboard_import_partition(
                blk,
                XEMU_XBOX_HDD_PARTITION_C_OFFSET,
                XEMU_XBOX_HDD_PARTITION_C_SIZE,
                source_root,
                backup_c,
                errp)) {
            return false;
        }
        imported_any = true;
    }

    if (!imported_any) {
        error_setg(errp, "No importable dashboard files were found.");
        return false;
    }

    return true;
}
