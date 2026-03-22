#include "redis/SessionStore.h"
#include "redis/RedisClient.h"
#include "util/UuidGenerator.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sos {

SessionStore::SessionStore(RedisClient& redis, std::chrono::seconds token_ttl,
                           std::chrono::seconds game_server_heartbeat_ttl)
    : redis_(redis)
    , token_ttl_(token_ttl)
    , game_server_heartbeat_ttl_(game_server_heartbeat_ttl)
{
}

std::string SessionStore::createToken(const std::string& player_id,
                                       const std::string& session_id) {
    auto token = generateUuid();

    nlohmann::json value;
    value["player_id"] = player_id;
    value["session_id"] = session_id;

    std::string key = "token:" + token;
    redis_.setex(key, value.dump(), token_ttl_);

    return token;
}

std::optional<TokenData> SessionStore::validateToken(const std::string& token) {
    std::string key = "token:" + token;
    auto result = redis_.get(key);

    if (!result) {
        return std::nullopt;
    }

    redis_.del(key); // 일회용: 검증 후 삭제

    try {
        auto json = nlohmann::json::parse(*result);
        TokenData data;
        data.player_id = json.at("player_id").get<std::string>();
        data.session_id = json.at("session_id").get<std::string>();
        return data;
    } catch (const std::exception& e) {
        spdlog::error("[SessionStore] Failed to parse token data, token={}, error={}",
                      token, e.what());
        return std::nullopt;
    }
}

void SessionStore::registerGameSession(const std::string& session_id) {
    redis_.sadd("active_sessions", session_id);
}

void SessionStore::unregisterGameSession(const std::string& session_id) {
    redis_.srem("active_sessions", session_id);
}

void SessionStore::updateGameServerHeartbeat(const std::string& server_id) {
    std::string key = "game_server:" + server_id;
    redis_.setex(key, "alive", game_server_heartbeat_ttl_);
}

bool SessionStore::isGameServerAlive(const std::string& server_id) {
    std::string key = "game_server:" + server_id;
    return redis_.exists(key);
}

} // namespace sos
