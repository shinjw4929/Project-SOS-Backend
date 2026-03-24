#include "ChannelManager.h"
#include "server/ChatSession.h"
#include "redis/RedisClient.h"

#include <spdlog/spdlog.h>

#include <chrono>

namespace sos {

ChannelManager::ChannelManager(uint32_t max_message_length, uint32_t history_size,
                               std::chrono::seconds session_ttl, RedisClient* redis)
    : max_message_length_(max_message_length)
    , history_size_(history_size)
    , session_ttl_(session_ttl)
    , redis_(redis)
{
}

// ============================================================
// Client Authentication
// ============================================================

void ChannelManager::handleAuth(const sos::chat::ChatAuth& auth,
                                std::shared_ptr<ChatSession> session) {
    const auto& player_id = auth.player_id();
    const auto& player_name = auth.player_name();
    const auto& session_id = auth.session_id();

    if (player_id.empty() || player_name.empty()) {
        sos::chat::ChatEnvelope env;
        auto* result = env.mutable_auth_result();
        result->set_success(false);
        result->set_reason("player_id and player_name are required");
        session->send(env);
        return;
    }

    // 동일 player_id 중복 접속 처리: 기존 연결 kick
    if (auto it = players_.find(player_id); it != players_.end()) {
        auto existing_session = it->second.session.lock();

        // 기존 상태를 먼저 정리 (close() 내 handleDisconnect 재진입 방지)
        lobby_members_.erase(player_id);
        if (!it->second.session_id.empty()) {
            auto sc_it = session_channels_.find(it->second.session_id);
            if (sc_it != session_channels_.end()) {
                sc_it->second.members.erase(player_id);
            }
        }
        players_.erase(it);

        // 상태 정리 후 기존 세션에 kick 메시지 전송
        if (existing_session && existing_session != session) {
            existing_session->setPlayerId("");
            existing_session->setAuthenticated(false);
            sos::chat::ChatEnvelope kick_env;
            auto* error = kick_env.mutable_error();
            error->set_code(sos::chat::ChatError::NOT_AUTHENTICATED);
            error->set_message("Replaced by new connection");
            existing_session->send(kick_env);
            spdlog::info("[Chat] Kicked existing connection, player_id={}", player_id);
        }
    }

    // 플레이어 상태 등록
    PlayerState state;
    state.player_name = player_name;
    state.session_id = session_id;
    state.session = session;

    if (session_id.empty()) {
        // 로비 모드
        state.in_lobby = true;
        lobby_members_.insert(player_id);
        spdlog::info("[Chat] Player authenticated (lobby), player_id={}, player_name={}",
                     player_id, player_name);
    } else {
        // 인게임 모드: 세션 채널 존재 확인
        auto sc_it = session_channels_.find(session_id);
        if (sc_it == session_channels_.end()) {
            sos::chat::ChatEnvelope env;
            auto* result = env.mutable_auth_result();
            result->set_success(false);
            result->set_reason("Session not found");
            session->send(env);
            return;
        }

        // 세션 채널에 참가
        sc_it->second.members.insert(player_id);

        // Redis에 플레이어-세션 매핑 저장
        if (redis_) {
            try {
                redis_->hset("chat:session:" + session_id, player_id, player_name);
                redis_->setex("chat:player:" + player_id, session_id, session_ttl_);
                redis_->setex("chat:name:" + player_id, player_name, session_ttl_);
                redis_->expire("chat:session:" + session_id, session_ttl_);
            } catch (const std::exception& e) {
                spdlog::error("[Chat] Redis error during session auth, player_id={}, error={}",
                             player_id, e.what());
            }
        }

        spdlog::info("[Chat] Player authenticated (session), player_id={}, session_id={}",
                     player_id, session_id);
    }

    players_[player_id] = std::move(state);
    session->setPlayerId(player_id);
    session->setAuthenticated(true);

    // 인증 성공 응답
    sos::chat::ChatEnvelope env;
    auto* result = env.mutable_auth_result();
    result->set_success(true);
    session->send(env);

    // 세션 채널 참가 시 히스토리 전달
    if (!session_id.empty()) {
        sendHistory(player_id, session_id);
    }
}

// ============================================================
// Chat Message
// ============================================================

void ChannelManager::handleChatSend(const std::string& player_id,
                                     const sos::chat::ChatSend& message) {
    auto player_it = players_.find(player_id);
    if (player_it == players_.end()) return;

    const auto& state = player_it->second;
    auto channel = message.channel();
    const auto& content = message.content();

    if (!validateMessage(player_id, content, channel)) return;

    // ChatReceive 생성
    sos::chat::ChatReceive receive;
    receive.set_channel(channel);
    receive.set_sender_id(player_id);
    receive.set_sender_name(state.player_name);
    receive.set_content(content);
    receive.set_timestamp(currentTimestampMs());

    sos::chat::ChatEnvelope envelope;
    *envelope.mutable_receive() = receive;

    switch (channel) {
        case sos::chat::CHANNEL_LOBBY:
            if (!state.in_lobby) {
                sendError(player_id, sos::chat::ChatError::CHANNEL_NOT_JOINED,
                         "Not in lobby channel");
                return;
            }
            broadcastToLobby(envelope);
            break;

        case sos::chat::CHANNEL_ALL:
            if (state.session_id.empty()) {
                sendError(player_id, sos::chat::ChatError::CHANNEL_NOT_JOINED,
                         "Not in a game session");
                return;
            }
            broadcastToSession(state.session_id, envelope);
            saveToHistory(state.session_id, receive);
            break;

        case sos::chat::CHANNEL_WHISPER: {
            const auto& target = message.whisper_target();
            if (target.empty()) {
                sendError(player_id, sos::chat::ChatError::PLAYER_NOT_FOUND,
                         "Whisper target not specified");
                return;
            }
            auto target_it = players_.find(target);
            if (target_it == players_.end()) {
                sendError(player_id, sos::chat::ChatError::PLAYER_NOT_FOUND,
                         "Player not found");
                return;
            }
            // 대상에게 전송
            sendTo(target, envelope);
            // 발신자에게도 에코
            sendTo(player_id, envelope);
            break;
        }

        default:
            sendError(player_id, sos::chat::ChatError::INVALID_CHANNEL,
                     "Invalid channel");
            break;
    }
}

// ============================================================
// Disconnect
// ============================================================

void ChannelManager::handleDisconnect(const std::string& player_id) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    const auto& state = it->second;

    lobby_members_.erase(player_id);

    if (!state.session_id.empty()) {
        auto sc_it = session_channels_.find(state.session_id);
        if (sc_it != session_channels_.end()) {
            sc_it->second.members.erase(player_id);
        }
    }

    players_.erase(it);
    spdlog::info("[Chat] Player disconnected, player_id={}", player_id);
}

// ============================================================
// Internal: Session Created / Ended
// ============================================================

void ChannelManager::handleSessionCreated(const sos::chat::SessionCreated& message) {
    const auto& session_id = message.session_id();

    SessionChannel channel;
    for (const auto& player : message.players()) {
        channel.expected_players.insert(player.player_id());

        // Redis에 플레이어 이름 저장
        if (redis_) {
            try {
                redis_->hset("chat:session:" + session_id,
                            player.player_id(), player.player_name());
                redis_->setex("chat:name:" + player.player_id(),
                            player.player_name(), session_ttl_);
            } catch (const std::exception& e) {
                spdlog::error("[Chat] Redis error saving session player, error={}", e.what());
            }
        }
    }

    session_channels_[session_id] = std::move(channel);

    if (redis_) {
        try {
            redis_->expire("chat:session:" + session_id, session_ttl_);
        } catch (const std::exception& e) {
            spdlog::error("[Chat] Redis error setting session TTL, error={}", e.what());
        }
    }

    spdlog::info("[Chat] Session created, session_id={}, players={}",
                 session_id, message.players_size());
}

void ChannelManager::handleSessionEnded(const sos::chat::SessionEnded& message) {
    const auto& session_id = message.session_id();

    auto sc_it = session_channels_.find(session_id);
    if (sc_it == session_channels_.end()) {
        spdlog::warn("[Chat] SessionEnded for unknown session, session_id={}", session_id);
        return;
    }

    // 세션 채널의 모든 멤버를 로비로 이동
    for (const auto& player_id : sc_it->second.members) {
        auto player_it = players_.find(player_id);
        if (player_it != players_.end()) {
            player_it->second.session_id.clear();
            player_it->second.in_lobby = true;
            lobby_members_.insert(player_id);
        }
    }

    session_channels_.erase(sc_it);

    // Redis 정리
    if (redis_) {
        try {
            redis_->del("chat:session:" + session_id);
            redis_->del("chat:history:" + session_id + ":ALL");
        } catch (const std::exception& e) {
            spdlog::error("[Chat] Redis error cleaning session, error={}", e.what());
        }
    }

    spdlog::info("[Chat] Session ended, session_id={}", session_id);
}

// ============================================================
// Internal Helpers
// ============================================================

void ChannelManager::sendTo(const std::string& player_id,
                             const sos::chat::ChatEnvelope& envelope) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    if (auto session = it->second.session.lock()) {
        session->send(envelope);
    }
}

void ChannelManager::sendError(const std::string& player_id,
                                sos::chat::ChatError_ChatErrorCode code,
                                const std::string& message) {
    sos::chat::ChatEnvelope envelope;
    auto* error = envelope.mutable_error();
    error->set_code(code);
    error->set_message(message);
    sendTo(player_id, envelope);
}

void ChannelManager::broadcastToLobby(const sos::chat::ChatEnvelope& envelope) {
    for (const auto& player_id : lobby_members_) {
        sendTo(player_id, envelope);
    }
}

void ChannelManager::broadcastToSession(const std::string& session_id,
                                         const sos::chat::ChatEnvelope& envelope) {
    auto it = session_channels_.find(session_id);
    if (it == session_channels_.end()) return;

    for (const auto& player_id : it->second.members) {
        sendTo(player_id, envelope);
    }
}

bool ChannelManager::validateMessage(const std::string& player_id,
                                      const std::string& content,
                                      sos::chat::ChatChannel channel) {
    // 빈 메시지 거부
    bool all_whitespace = true;
    for (char c : content) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            all_whitespace = false;
            break;
        }
    }
    if (content.empty() || all_whitespace) {
        sendError(player_id, sos::chat::ChatError::MESSAGE_TOO_LONG,
                 "Message cannot be empty");
        return false;
    }

    // 길이 제한 (UTF-8 바이트 기준)
    if (content.size() > max_message_length_) {
        sendError(player_id, sos::chat::ChatError::MESSAGE_TOO_LONG,
                 "Message too long (max " + std::to_string(max_message_length_) + " bytes)");
        return false;
    }

    return true;
}

uint64_t ChannelManager::currentTimestampMs() {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

void ChannelManager::saveToHistory(const std::string& session_id,
                                    const sos::chat::ChatReceive& receive) {
    if (!redis_) return;

    try {
        std::string key = "chat:history:" + session_id + ":ALL";
        std::string serialized = receive.SerializeAsString();
        redis_->lpush(key, serialized);
        redis_->ltrim(key, 0, static_cast<int64_t>(history_size_) - 1);
        redis_->expire(key, session_ttl_);
    } catch (const std::exception& e) {
        spdlog::error("[Chat] Failed to save history, session_id={}, error={}",
                     session_id, e.what());
    }
}

void ChannelManager::sendHistory(const std::string& player_id,
                                  const std::string& session_id) {
    if (!redis_) return;

    try {
        std::string key = "chat:history:" + session_id + ":ALL";
        auto messages = redis_->lrange(key, 0, static_cast<int64_t>(history_size_) - 1);

        // Redis LIST는 최신이 앞에 있으므로 역순으로 전송 (오래된 것 먼저)
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            sos::chat::ChatReceive receive;
            if (receive.ParseFromString(*it)) {
                sos::chat::ChatEnvelope envelope;
                *envelope.mutable_receive() = receive;
                sendTo(player_id, envelope);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("[Chat] Failed to send history, player_id={}, error={}",
                     player_id, e.what());
    }
}

} // namespace sos
