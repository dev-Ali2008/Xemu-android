#include "virtual_dvd_drive.h"
#include <android/log.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

#define LOG_TAG "VirtualDVDDrive"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

VirtualDVDDrive::VirtualDVDDrive()
    : isISOMounted(false)
    , totalSectors(0)
    , sectorSize(2048)  
    , isoFileHandle(-1)
    , mappedISOData(nullptr)
    , mappedISOSize(0)
{
    LOGD("VirtualDVDDrive constructor called");
}

VirtualDVDDrive::~VirtualDVDDrive()
{
    LOGD("VirtualDVDDrive destructor called");
    if (isMounted()) {
        unmountISO();
    }
}

bool VirtualDVDDrive::mountISO(const std::string& isoPath)
{
    LOGI("Attempting to mount ISO: %s", isoPath.c_str());

    if (isMounted()) {
        LOGW("ISO already mounted, unmounting first");
        unmountISO();
    }


    isoFileHandle = open(isoPath.c_str(), O_RDONLY);
    if (isoFileHandle == -1) {
        LOGE("Failed to open ISO file: %s (errno: %d)", strerror(errno), errno);
        return false;
    }


    struct stat fileStat;
    if (fstat(isoFileHandle, &fileStat) == -1) {
        LOGE("Failed to get file stats: %s", strerror(errno));
        close(isoFileHandle);
        isoFileHandle = -1;
        return false;
    }

    mappedISOSize = fileStat.st_size;
    LOGI("ISO file size: %zu bytes", mappedISOSize);


    totalSectors = static_cast<uint32_t>(mappedISOSize / sectorSize);
    LOGI("Total sectors: %u", totalSectors);


    mappedISOData = mmap(nullptr, mappedISOSize, PROT_READ, MAP_PRIVATE, isoFileHandle, 0);
    if (mappedISOData == MAP_FAILED) {
        LOGE("Failed to memory map ISO file: %s", strerror(errno));
        close(isoFileHandle);
        isoFileHandle = -1;
        return false;
    }

    mountedISOPath = isoPath;
    isISOMounted = true;

    LOGI("Successfully mounted ISO: %s", isoPath.c_str());
    return true;
}

void VirtualDVDDrive::unmountISO()
{
    if (!isMounted()) {
        LOGD("No ISO mounted to unmount");
        return;
    }

    LOGI("Unmounting ISO: %s", mountedISOPath.c_str());


    if (mappedISOData != nullptr) {
        munmap(mappedISOData, mappedISOSize);
        mappedISOData = nullptr;
        mappedISOSize = 0;
    }


    if (isoFileHandle != -1) {
        close(isoFileHandle);
        isoFileHandle = -1;
    }


    mountedISOPath.clear();
    isISOMounted = false;
    totalSectors = 0;

    LOGI("ISO unmounted successfully");
}

bool VirtualDVDDrive::isMounted() const
{
    return isISOMounted;
}

std::string VirtualDVDDrive::getMountedISOPath() const
{
    return mountedISOPath;
}

std::vector<uint8_t> VirtualDVDDrive::readSectors(uint32_t sector, uint32_t count)
{
    if (!isMounted()) {
        LOGE("Cannot read sectors: no ISO mounted");
        return std::vector<uint8_t>();
    }

    if (sector >= totalSectors) {
        LOGE("Sector %u out of range (max: %u)", sector, totalSectors - 1);
        return std::vector<uint8_t>();
    }

    if (sector + count > totalSectors) {
        LOGW("Read request exceeds available sectors, adjusting count");
        count = totalSectors - sector;
    }

    size_t startOffset = static_cast<size_t>(sector) * sectorSize;
    size_t readSize = static_cast<size_t>(count) * sectorSize;

    if (startOffset + readSize > mappedISOSize) {
        LOGE("Read request exceeds mapped file size");
        return std::vector<uint8_t>();
    }

    LOGD("Reading %u sectors starting from sector %u (offset: %zu, size: %zu)", 
         count, sector, startOffset, readSize);


    std::vector<uint8_t> data(readSize);
    memcpy(data.data(), static_cast<const uint8_t*>(mappedISOData) + startOffset, readSize);

    return data;
}

uint32_t VirtualDVDDrive::getTotalSectors() const
{
    return totalSectors;
}

uint32_t VirtualDVDDrive::getSectorSize() const
{
    return sectorSize;
}
