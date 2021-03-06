#pragma once

#include <Device.h>
#include <Net/Net.h>
#include <Scheduler.h>

enum {
    LinkDown,
    LinkUp,
};

namespace Network{
    class NetworkAdapter : public Device {
        friend class NetFS;

    public:
        enum AdapterType {
            NetworkAdapterLoopback,
            NetworkAdapterEthernet,
        };

    protected:

        static int nextDeviceNumber;
        unsigned maxCache = 256; // Maximum amount of cached packets, NIC drivers are free to change this
    
        int linkState = LinkDown;

        FastList<NetworkPacket*> cache;
        FastList<NetworkPacket*> queue;

        Semaphore packetSemaphore = Semaphore(0);

        lock_t cacheLock = 0;
        lock_t queueLock = 0;

        lock_t threadLock = 0;

        AdapterType type;

    public:
        enum DriverState{
            OK,
            Uninitialized,
            Error,
        };
        DriverState dState = Uninitialized;

        MACAddress mac;

        // All of these are big-endian
        IPv4Address adapterIP = 0; // 0.0.0.0
        IPv4Address gatewayIP = 0; // 0.0.0.0
        IPv4Address subnetMask = 0xFFFFFFFF; // 255.255.255.255 (no subnet)
        int adapterIndex = 0; // Index in the adapters list
        
        NetworkAdapter(AdapterType aType);

        virtual int Ioctl(uint64_t cmd, uint64_t arg);
        
        virtual void SendPacket(void* data, size_t len);

        virtual int GetLink() { return linkState; }
        virtual int QueueSize() { return queue.get_length(); }
        virtual NetworkPacket* Dequeue() { 

            if(queue.get_length()) {
                packetSemaphore.SetValue(queue.get_length() - 1);
                return queue.remove_at(0); 
            } else {
                return nullptr;
            }
        }
        virtual NetworkPacket* DequeueBlocking() {
            if(packetSemaphore.Wait()){
                return nullptr; // We were interrupted
            }

            if(queue.get_length()){
                return queue.remove_at(0); 
            } else {
                return nullptr;
            }
        }
        virtual void CachePacket(NetworkPacket* pkt){
            if(cache.get_length() < maxCache){
                acquireLock(&cacheLock);

                cache.add_back(pkt);

                releaseLock(&cacheLock);
            } else {
                delete pkt;
            }
        };

        virtual ~NetworkAdapter() = default;
    };

    extern NetworkAdapter* mainAdapter;

    void AddAdapter(NetworkAdapter* a);
    void FindMainAdapter();
}