#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <condition_variable>
#include <deque>

class PacketManager
{
public:
    struct DataPacket
    {
        int packetNumber;
        std::vector<uint8_t> packetData;
        const char *data() const
        {
            return reinterpret_cast<const char *>(packetData.data());
        }

        size_t size() const
        {
            return packetData.size();
        }
    };

    PacketManager(size_t bufferSize, size_t packetSize)
        : buffer(bufferSize), packetSize(packetSize), nextPacketNumber(0), writePos(0), readPos(0), dataSize(0) {}

    void SplitIntoPackets(const char *data, size_t len)
    {
        size_t offset = 0;
        while (offset < len)
        {
            std::unique_lock<std::mutex> lock(bufferMutex);

            size_t remainingBytes = len - offset;
            size_t packetDataLength = std::min(packetSize, remainingBytes);

            if (dataSize + packetDataLength > buffer.size())
            {
                // Buffer overflow, wait until there is enough space
                bufferNotFull.wait(lock, [&]
                                   { return dataSize + packetDataLength <= buffer.size(); });
            }

            DataPacket packet;
            packet.packetNumber = nextPacketNumber++;
            packet.packetData.resize(packetDataLength);
            std::memcpy(packet.packetData.data(), data + offset, packetDataLength);

            packets.push_back(packet);
            size_t endPos = (writePos + packetDataLength) % buffer.size();
            if (endPos < writePos)
            {
                std::memcpy(buffer.data() + writePos, data + offset, buffer.size() - writePos);
                std::memcpy(buffer.data(), data + offset + (buffer.size() - writePos), endPos);
            }
            else
            {
                std::memcpy(buffer.data() + writePos, data + offset, packetDataLength);
            }

            writePos = endPos;
            dataSize += packetDataLength;
            offset += packetDataLength;

            bufferNotEmpty.notify_one();
        }
    }

    bool TryGetNextPacket(DataPacket &packet)
    {
        std::unique_lock<std::mutex> ul(bufferMutex);
        bufferNotEmpty.wait(ul, [&]
                            { return !packets.empty(); });

        if (!packets.empty())
        {
            packet = packets.front();
            packets.pop_front();

            size_t packetDataLength = packet.packetData.size();
            readPos = (readPos + packetDataLength) % buffer.size();
            dataSize -= packetDataLength;

            bufferNotFull.notify_one();
            return true;
        }

        return false;
    }

private:
    std::vector<uint8_t> buffer;
    size_t packetSize;
    std::atomic<int> nextPacketNumber;
    size_t writePos;
    size_t readPos;
    size_t dataSize;
    typedef std::deque<DataPacket> PacketQueue;
    PacketQueue packets;
    std::mutex bufferMutex;
    std::condition_variable bufferNotFull;
    std::condition_variable bufferNotEmpty;
};