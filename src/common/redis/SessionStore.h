#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace sos {

class RedisClient;

struct TokenData {
    std::string player_id;
    std::string session_id;
};

class SessionStore {
public:
    SessionStore(RedisClient& redis, std::chrono::seconds token_ttl,
                 std::chrono::seconds game_server_heartbeat_ttl);

    std::string createToken(const std::string& player_id, const std::string& session_id);
    std::optional<TokenData> validateToken(const std::string& token);

    void registerGameSession(const std::string& session_id);
    void unregisterGameSession(const std::string& session_id);

    void updateGameServerHeartbeat(const std::string& server_id);
    bool isGameServerAlive(const std::string& server_id);

private:
    RedisClient& redis_;
    std::chrono::seconds token_ttl_;
    std::chrono::seconds game_server_heartbeat_ttl_;
};

} // namespace sos
