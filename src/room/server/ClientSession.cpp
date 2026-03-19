#include "ClientSession.h"
#include "room/RoomManager.h"

#include <spdlog/spdlog.h>

namespace sos {

ClientSession::ClientSession(boost::asio::ip::tcp::socket socket,
                             std::shared_ptr<RoomManager> room_manager)
    : socket_(std::move(socket))
    , room_manager_(std::move(room_manager))
{
}

ClientSession::~ClientSession() {
    close();
}

void ClientSession::start() {
    spdlog::info("[Room] Client connected, remote={}", remoteAddress());
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
            }

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
            // Phase 3에서 타임아웃 처리 구현 예정
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

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    if (!player_id_.empty()) {
        room_manager_->handleDisconnect(player_id_);
        player_id_.clear();
    }
}

} // namespace sos
