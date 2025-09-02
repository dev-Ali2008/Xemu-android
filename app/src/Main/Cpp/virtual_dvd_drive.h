#ifndef VIRTUAL_DVD_DRIVE_H
#define VIRTUAL_DVD_DRIVE_H

#include <string>
#include <vector>
#include <memory>





class VirtualDVDDrive {
public:
    VirtualDVDDrive();
    ~VirtualDVDDrive();






    bool mountISO(const std::string& isoPath);




    void unmountISO();





    bool isMounted() const;





    std::string getMountedISOPath() const;







    std::vector<uint8_t> readSectors(uint32_t sector, uint32_t count);





    uint32_t getTotalSectors() const;





    uint32_t getSectorSize() const;

private:
    std::string mountedISOPath;
    bool isISOMounted;
    uint32_t totalSectors;
    uint32_t sectorSize;


    int isoFileHandle;


    void* mappedISOData;
    size_t mappedISOSize;
};

#endif 
