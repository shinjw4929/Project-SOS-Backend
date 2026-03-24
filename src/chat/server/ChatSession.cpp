#include "ChatSession.h"
#include "channel/ChannelManager.h"
#include "ratelimit/RateLimiter.h"

#include <spdlog/spdlog.h>

namespace sos {

ChatSession::ChatSession(boost::asio::ip::tcp::socket socket,
                         std::shared_ptr<ChannelManager> channel_manager,
                         RateLimiter* rate_limiter,
                         std::chrono::seconds heartbeat_timeout)
    : socket_(std::move(socket))
    , heartbeat_timer_(socket_.get_executor())
    , channel_manager_(std::move(channel_manager))
    , rate_limiter_(rate_limiter)
    , heartbeat_timeout_(heartbeat_timeout)
{
}

ChatSession::~ChatSession() {
    close();
}

void ChatSession::start() {
    spdlog::info("[Chat] Client connected, remote={}", remoteAddress());
    resetHeartbeatTimer();
    doRead();
}

void ChatSession::send(const sos::chat::ChatEnvelope& envelope) {
    if (closed_) return;
    bool was_idle = write_queue_.empty();
    write_queue_.push_back(Codec<sos::chat::ChatEnvelope>::encode(envelope));
    if (was_idle) {
        doWrite();
    }
}

std::string ChatSession::remoteAddress() const {
    try {
        auto endpoint = socket_.remote_endpoint();
        return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    } catch (...) {
        return "unknown";
    }
}

// ============================================================
// Heartbeat Timer
// ============================================================

void ChatSession::resetHeartbeatTimer() {
    if (heartbeat_timeout_.count() <= 0) return;

    heartbeat_timer_.expires_after(heartbeat_timeout_);
    auto self = shared_from_this();
    heartbeat_timer_.async_wait([this, self](boost::system::error_code ec) {
        if (ec) return;
        spdlog::warn("[Chat] Heartbeat timeout, remote={}, player_id={}",
                     remoteAddress(), player_id_);
        close();
    });
}

// ============================================================
// Async I/O
// ============================================================

void ChatSession::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this, self](boost::system::error_code ec, size_t bytes_transferred) {
            if (ec) {
                if (ec != boost::asio::error::eof &&
                    ec != boost::asio::error::operation_aborted) {
                    spdlog::warn("[Chat] Read error, remote={}, error={}",
                                 remoteAddress(), ec.message());
                }
                close();
                return;
            }

            codec_.feed(read_buffer_.data(), bytes_transferred);

            while (auto envelope = codec_.tryDecode()) {
                processMessage(*envelope);
                if (closed_) return;
            }

            resetHeartbeatTimer();
            doRead();
        });
}

void ChatSession::doWrite() {
    if (write_queue_.empty()) return;

    auto self = shared_from_this();
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),
        [this, self](boost::system::error_code ec, size_t /*bytes_transferred*/) {
            if (ec) {
                spdlog::warn("[Chat] Write error, remote={}, error={}",
                             remoteAddress(), ec.message());
                close();
                return;
            }

            write_queue_.pop_front();
            if (!write_queue_.empty()) {
                doWrite();
            }
        });
}

// ============================================================
// Message Dispatch
// ============================================================

void ChatSession::processMessage(const sos::chat::ChatEnvelope& envelope) {
    using Payload = sos::chat::ChatEnvelope;

    switch (envelope.payload_case()) {
        case Payload::kAuth:
            channel_manager_->handleAuth(envelope.auth(), shared_from_this());
            break;

        case Payload::kSend:
            if (!authenticated_) {
                sos::chat::ChatEnvelope error_env;
                auto* error = error_env.mutable_error();
                error->set_code(sos::chat::ChatError::NOT_AUTHENTICATED);
                error->set_message("Not authenticated");
                send(error_env);
                return;
            }
            // 채팅 Rate Limit (player_id 기반)
            if (rate_limiter_) {
                try {
                    if (!rate_limiter_->allow(player_id_)) {
                        sos::chat::ChatEnvelope error_env;
                        auto* error = error_env.mutable_error();
                        error->set_code(sos::chat::ChatError::RATE_LIMITED);
                        error->set_message("Too many messages");
                        send(error_env);
                        return;
                    }
                } catch (const std::exception& e) {
                    spdlog::error("[Chat] Rate limit check failed, player_id={}, error={}",
                                 player_id_, e.what());
                }
            }
            channel_manager_->handleChatSend(player_id_, envelope.send());
            break;

        case Payload::kHeartbeat:
            spdlog::debug("[Chat] Heartbeat received, player_id={}", player_id_);
            break;

        default:
            spdlog::warn("[Chat] Unknown message, remote={}, payload_case={}",
                         remoteAddress(), static_cast<int>(envelope.payload_case()));
            break;
    }
}

void ChatSession::close() {
    if (closed_) return;
    closed_ = true;

    heartbeat_timer_.cancel();

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    if (!player_id_.empty()) {
        channel_manager_->handleDisconnect(player_id_);
        player_id_.clear();
    }
}

} // namespace sos
