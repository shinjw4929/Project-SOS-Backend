#pragma once

#include "protocol/Codec.h"

#include <chat.pb.h>

#include <boost/asio.hpp>

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace sos {

class ChannelManager;
class RateLimiter;

class ChatSession : public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(boost::asio::ip::tcp::socket socket,
                std::shared_ptr<ChannelManager> channel_manager,
                RateLimiter* rate_limiter,
                std::chrono::seconds heartbeat_timeout);
    ~ChatSession();

    void start();
    void send(const sos::chat::ChatEnvelope& envelope);
    void close();

    const std::string& playerId() const { return player_id_; }
    void setPlayerId(const std::string& player_id) { player_id_ = player_id; }
    bool isAuthenticated() const { return authenticated_; }
    void setAuthenticated(bool value) { authenticated_ = value; }

    std::string remoteAddress() const;

private:
    void doRead();
    void doWrite();
    void processMessage(const sos::chat::ChatEnvelope& envelope);
    void resetHeartbeatTimer();

    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer heartbeat_timer_;
    std::shared_ptr<ChannelManager> channel_manager_;
    RateLimiter* rate_limiter_;
    Codec<sos::chat::ChatEnvelope> codec_;

    std::string player_id_;
    std::chrono::seconds heartbeat_timeout_;
    bool authenticated_ = false;
    bool closed_ = false;

    std::array<uint8_t, 8192> read_buffer_{};
    std::deque<std::vector<uint8_t>> write_queue_;
};

} // namespace sos
