#include "ClientSession.h"
#include "room/RoomManager.h"
#include "ratelimit/RateLimiter.h"

#include <spdlog/spdlog.h>

namespace sos {

ClientSession::ClientSession(boost::asio::ip::tcp::socket socket,
                             std::shared_ptr<RoomManager> room_manager,
                             RateLimiter* rate_limiter,
                             std::chrono::seconds heartbeat_timeout)
    : socket_(std::move(socket))
    , heartbeat_timer_(socket_.get_executor())
    , room_manager_(std::move(room_manager))
    , rate_limiter_(rate_limiter)
    , heartbeat_timeout_(heartbeat_timeout)
{
}

ClientSession::~ClientSession() {
    close();
}

void ClientSession::start() {
    try {
        remote_ip_ = socket_.remote_endpoint().address().to_string();
    } catch (...) {
        remote_ip_ = "unknown";
    }
    spdlog::info("[Room] Client connected, remote={}", remoteAddress());
    resetHeartbeatTimer();
    doRead();
}

void ClientSession::send(const sos::room::Envelope& envelope) {
    bool was_idle = write_queue_.empty();
    write_queue_.push_back(Codec<sos::room::Envelope>::encode(envelope));
    if (was_idle) {
        doWrite();
    }
}

std::string ClientSession::remoteAddress() const {
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

void ClientSession::resetHeartbeatTimer() {
    if (heartbeat_timeout_.count() <= 0) return;

    heartbeat_timer_.expires_after(heartbeat_timeout_);
    auto self = shared_from_this();
    heartbeat_timer_.async_wait([this, self](boost::system::error_code ec) {
        if (ec) return; // cancelled or error
        spdlog::warn("[Room] Heartbeat timeout, remote={}", remoteAddress());
        close();
    });
}

// ============================================================
// Async I/O
// ============================================================

void ClientSession::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this, self](boost::system::error_code ec, size_t bytes_transferred) {
            if (ec) {
                if (ec != boost::asio::error::eof &&
                    ec != boost::asio::error::operation_aborted) {
                    spdlog::warn("[Room] Read error, remote={}, error={}",
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

void ClientSession::doWrite() {
    if (write_queue_.empty()) return;

    auto self = shared_from_this();
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),
        [this, self](boost::system::error_code ec, size_t /*bytes_transferred*/) {
            if (ec) {
                spdlog::warn("[Room] Write error, remote={}, error={}",
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

void ClientSession::processMessage(const sos::room::Envelope& envelope) {
    // Rate limit 검사
    if (rate_limiter_) {
        try {
            if (!rate_limiter_->allow(remote_ip_)) {
                spdlog::warn("[Room] Rate limited, remote={}", remoteAddress());
                sos::room::Envelope reject_env;
                auto* reject = reject_env.mutable_reject();
                reject->set_reason(sos::room::RejectResponse::RATE_LIMITED);
                reject->set_message("Too many requests");
                send(reject_env);
                close();
                return;
            }
        } catch (const std::exception& e) {
            // Redis 장애 시 fail-open (요청 허용)
            spdlog::error("[Room] Rate limit check failed, remote={}, error={}",
                         remoteAddress(), e.what());
        }
    }

    using Payload = sos::room::Envelope;

    switch (envelope.payload_case()) {
        case Payload::kCreateRoom:
            room_manager_->handleCreateRoom(envelope.create_room(), shared_from_this());
            break;

        case Payload::kJoinRoom:
            room_manager_->handleJoinRoom(envelope.join_room(), shared_from_this());
            break;

        case Payload::kLeaveRoom:
            if (!player_id_.empty()) {
                room_manager_->handleLeaveRoom(player_id_);
            }
            break;

        case Payload::kToggleReady:
            if (!player_id_.empty()) {
                room_manager_->handleToggleReady(player_id_);
            }
            break;

        case Payload::kStartGame:
            if (!player_id_.empty()) {
                room_manager_->handleStartGame(player_id_);
            }
            break;

        case Payload::kRoomListRequest:
            room_manager_->handleRoomListRequest(envelope.room_list_request(), shared_from_this());
            break;

        case Payload::kHeartbeat:
            // 타이머 리셋은 doRead()에서 처리
            spdlog::debug("[Room] Heartbeat received, remote={}", remoteAddress());
            break;

        default:
            spdlog::warn("[Room] Unknown message, remote={}, payload_case={}",
                         remoteAddress(), static_cast<int>(envelope.payload_case()));
            break;
    }
}

void ClientSession::close() {
    if (closed_) return;
    closed_ = true;

    heartbeat_timer_.cancel();

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    if (!player_id_.empty()) {
        room_manager_->handleDisconnect(player_id_);
        player_id_.clear();
    }
}

} // namespace sos
