
#include <vector>
#include <cstdint>
#include <cstring>
namespace AI_MSG
{
    
struct Data {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float32_t score;
    int32_t class_id;
};

std::vector<uint8_t> serialize(const std::vector<Data>& dataArray) {
    std::vector<uint8_t> buffer;
    for (const auto& data : dataArray) {
        buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&data), reinterpret_cast<const uint8_t*>(&data) + sizeof(Data));
    }
    return buffer;
}

std::vector<Data> deserialize(const std::vector<uint8_t>& buffer) {
    std::vector<Data> dataArray;
    size_t dataSize = sizeof(Data);
    for (size_t i = 0; i < buffer.size(); i += dataSize) {
        Data data;
        std::memcpy(&data, &buffer[i], dataSize);
        dataArray.push_back(data);
    }
    return dataArray;
}
} // namespace name

