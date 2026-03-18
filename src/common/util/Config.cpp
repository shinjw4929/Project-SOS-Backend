#include "util/Config.h"

#include <fstream>
#include <stdexcept>

namespace sos {

Config::Config(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Config file not found: " + filepath);
    }
    data_ = nlohmann::json::parse(file);
}

uint16_t Config::roomPort() const { return get<uint16_t>("room_port", 8080); }
uint16_t Config::internalPort() const { return get<uint16_t>("internal_port", 8081); }
std::string Config::redisHost() const { return get<std::string>("redis_host", "127.0.0.1"); }
uint16_t Config::redisPort() const { return get<uint16_t>("redis_port", 6379); }
std::string Config::redisPassword() const { return get<std::string>("redis_password", ""); }
uint32_t Config::maxRooms() const { return get<uint32_t>("max_rooms", 100); }
uint32_t Config::maxPlayersPerRoom() const { return get<uint32_t>("max_players_per_room", 8); }
uint32_t Config::tokenTtlSeconds() const { return get<uint32_t>("token_ttl_seconds", 60); }
uint32_t Config::heartbeatTimeoutSeconds() const { return get<uint32_t>("heartbeat_timeout_seconds", 30); }
uint32_t Config::gameServerHeartbeatTtlSeconds() const { return get<uint32_t>("game_server_heartbeat_ttl_seconds", 90); }
uint32_t Config::rateLimitMax() const { return get<uint32_t>("rate_limit_max", 20); }
uint32_t Config::rateLimitWindowSeconds() const { return get<uint32_t>("rate_limit_window_seconds", 10); }
std::string Config::gameServerHost() const { return get<std::string>("game_server_host", "127.0.0.1"); }
uint16_t Config::gameServerPort() const { return get<uint16_t>("game_server_port", 7979); }

} // namespace sos
