#pragma once

#include <stdint.h>
#include <list.h>
#include <fs/filesystem.h>

enum DeviceType{
    TypeGenericDevice,
    TypeDiskDevice,
    TypePartitionDevice,
    TypeNetworkAdapterDevice,
    TypeInputDevice,
};

class Device : public FsNode {
public:
    Device(){
        
    }

    Device(const char* name, DeviceType type){
        this->name = strdup(name);
        this->type = type;
    }

    const char* GetName() const{
        return name;
    }
protected:
    char* name = "";
    DeviceType type = TypeGenericDevice;
};

class PartitionDevice;

class DiskDevice : public Device{
    friend class PartitionDevice;
protected:
    int nextPartitionNumber = 0;
public:
    DiskDevice();

    int InitializePartitions();

    virtual int Read(uint64_t lba, uint32_t count, void* buffer);
    virtual int Write(uint64_t lba, uint32_t count, void* buffer);

    virtual ~DiskDevice();
    
    List<PartitionDevice*> partitions;
    int blocksize = 512;
private:
};

class PartitionDevice : public Device{
public:
    PartitionDevice(uint64_t startLBA, uint64_t endLBA, DiskDevice* disk);

    virtual int ReadAbsolute(uint64_t off, uint32_t count, void* buffer);
    virtual int Read(uint64_t lba, uint32_t count, void* buffer);
    virtual int Write(uint64_t lba, uint32_t count, void* buffer);
    
    virtual ~PartitionDevice();

    DiskDevice* parentDisk;
private:

    uint64_t startLBA;
    uint64_t endLBA;

    int type = TypePartitionDevice;
};

namespace DeviceManager{
    /////////////////////////////
    /// \brief Initialize basic devices (null, urandom, etc.)
    /////////////////////////////
    void InitializeBasicDevices();

    /////////////////////////////
    /// \brief Register a device
    ///
    /// Add device to devfs
    ///
    /// \param dev reference to Device to add
    /////////////////////////////
    void RegisterDevice(Device& dev);

    /////////////////////////////
    /// \brief Get devfs
    ///
    /// \return FsNode* object representing devfs root
    /////////////////////////////
    FsNode* GetDevFS();
}