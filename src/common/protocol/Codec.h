#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace sos {

static constexpr uint32_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB

template<typename ProtoMessage>
class Codec {
    std::vector<uint8_t> buffer_;

public:
    void feed(const uint8_t* data, size_t size) {
        buffer_.insert(buffer_.end(), data, data + size);
    }

    std::optional<ProtoMessage> tryDecode() {
        if (buffer_.size() < 4) return std::nullopt;

        uint32_t length = static_cast<uint32_t>(buffer_[0])
                        | (static_cast<uint32_t>(buffer_[1]) << 8)
                        | (static_cast<uint32_t>(buffer_[2]) << 16)
                        | (static_cast<uint32_t>(buffer_[3]) << 24);

        if (length > MAX_MESSAGE_SIZE) {
            buffer_.clear();
            return std::nullopt;
        }

        if (buffer_.size() < 4 + length) return std::nullopt;

        ProtoMessage message;
        if (!message.ParseFromArray(buffer_.data() + 4, static_cast<int>(length))) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + 4 + length);
            return std::nullopt;
        }

        buffer_.erase(buffer_.begin(), buffer_.begin() + 4 + length);
        return message;
    }

    static std::vector<uint8_t> encode(const ProtoMessage& message) {
        std::string serialized = message.SerializeAsString();
        uint32_t length = static_cast<uint32_t>(serialized.size());

        std::vector<uint8_t> result(4 + length);
        result[0] = static_cast<uint8_t>(length);
        result[1] = static_cast<uint8_t>(length >> 8);
        result[2] = static_cast<uint8_t>(length >> 16);
        result[3] = static_cast<uint8_t>(length >> 24);
        std::memcpy(result.data() + 4, serialized.data(), length);
        return result;
    }
};

} // namespace sos
