#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace sos {

class Config {
public:
    explicit Config(const std::string& filepath);

    template<typename T>
    T get(const std::string& key, const T& default_value) const {
        if (data_.contains(key)) {
            return data_.at(key).get<T>();
        }
        return default_value;
    }

    uint16_t roomPort() const;
    uint16_t internalPort() const;
    std::string redisHost() const;
    uint16_t redisPort() const;
    std::string redisPassword() const;
    uint32_t maxRooms() const;
    uint32_t maxPlayersPerRoom() const;
    uint32_t tokenTtlSeconds() const;
    uint32_t heartbeatTimeoutSeconds() const;
    uint32_t gameServerHeartbeatTtlSeconds() const;
    uint32_t rateLimitMax() const;
    uint32_t rateLimitWindowSeconds() const;
    std::string gameServerHost() const;
    uint16_t gameServerPort() const;

    // Chat Server
    uint16_t chatPort() const;
    uint16_t chatInternalPort() const;
    uint32_t chatHeartbeatTimeoutSeconds() const;
    uint32_t chatRateLimitMax() const;
    uint32_t chatRateLimitWindowSeconds() const;
    uint32_t chatMaxMessageLength() const;
    uint32_t chatHistorySize() const;
    uint32_t chatSessionTtlSeconds() const;
    std::string chatServerHost() const;
    uint16_t chatServerPort() const;

private:
    nlohmann::json data_;
};

} // namespace sos
