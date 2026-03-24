#pragma once

#include <chat.pb.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sos {

class ChatSession;
class RedisClient;

class ChannelManager {
public:
    ChannelManager(uint32_t max_message_length, uint32_t history_size,
                   std::chrono::seconds session_ttl, RedisClient* redis);

    // 클라이언트 인증
    void handleAuth(const sos::chat::ChatAuth& auth,
                    std::shared_ptr<ChatSession> session);

    // 채팅 메시지 처리
    void handleChatSend(const std::string& player_id,
                        const sos::chat::ChatSend& message);

    // 클라이언트 연결 해제
    void handleDisconnect(const std::string& player_id);

    // 내부: 세션 생성/종료 (Room Server -> Chat Server)
    void handleSessionCreated(const sos::chat::SessionCreated& message);
    void handleSessionEnded(const sos::chat::SessionEnded& message);

private:
    void sendTo(const std::string& player_id, const sos::chat::ChatEnvelope& envelope);
    void sendError(const std::string& player_id,
                   sos::chat::ChatError_ChatErrorCode code,
                   const std::string& message);
    void broadcastToLobby(const sos::chat::ChatEnvelope& envelope);
    void broadcastToSession(const std::string& session_id,
                            const sos::chat::ChatEnvelope& envelope);

    // 메시지 검증
    bool validateMessage(const std::string& player_id, const std::string& content,
                         sos::chat::ChatChannel channel);

    // 타임스탬프 생성 (밀리초)
    static uint64_t currentTimestampMs();

    // 히스토리 저장 (Redis LIST)
    void saveToHistory(const std::string& session_id,
                       const sos::chat::ChatReceive& receive);
    void sendHistory(const std::string& player_id, const std::string& session_id);

    struct PlayerState {
        std::string player_name;
        std::string session_id; // empty = lobby
        std::weak_ptr<ChatSession> session;
        bool in_lobby = false;
    };

    struct SessionChannel {
        std::unordered_set<std::string> members;
        std::unordered_set<std::string> expected_players;
    };

    uint32_t max_message_length_;
    uint32_t history_size_;
    std::chrono::seconds session_ttl_;
    RedisClient* redis_;

    std::unordered_map<std::string, PlayerState> players_;
    std::unordered_set<std::string> lobby_members_;
    std::unordered_map<std::string, SessionChannel> session_channels_;
};

} // namespace sos
