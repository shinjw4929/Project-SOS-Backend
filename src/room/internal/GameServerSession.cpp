#include "GameServerSession.h"
#include "redis/SessionStore.h"
#include "room/RoomManager.h"

#include <spdlog/spdlog.h>

namespace sos {

GameServerSession::GameServerSession(boost::asio::ip::tcp::socket socket,
                                     std::shared_ptr<SessionStore> session_store,
                                     std::shared_ptr<RoomManager> room_manager)
    : socket_(std::move(socket))
    , session_store_(std::move(session_store))
    , room_manager_(std::move(room_manager))
{
}

GameServerSession::~GameServerSession() {
    close();
}

void GameServerSession::start() {
    try {
        spdlog::info("[Room:Internal] Game server connected, remote={}",
                     socket_.remote_endpoint().address().to_string());
    } catch (...) {
        spdlog::info("[Room:Internal] Game server connected");
    }
    doRead();
}

void GameServerSession::send(const sos::room::Envelope& envelope) {
    bool was_idle = write_queue_.empty();
    write_queue_.push_back(Codec<sos::room::Envelope>::encode(envelope));
    if (was_idle) {
        doWrite();
    }
}

// ============================================================
// Async I/O
// ============================================================

void GameServerSession::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this, self](boost::system::error_code ec, size_t bytes_transferred) {
            if (ec) {
                if (ec != boost::asio::error::eof &&
                    ec != boost::asio::error::operation_aborted) {
                    spdlog::warn("[Room:Internal] Read error, error={}", ec.message());
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

void GameServerSession::doWrite() {
    if (write_queue_.empty()) return;

    auto self = shared_from_this();
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),
        [this, self](boost::system::error_code ec, size_t /*bytes_transferred*/) {
            if (ec) {
                spdlog::warn("[Room:Internal] Write error, error={}", ec.message());
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

void GameServerSession::processMessage(const sos::room::Envelope& envelope) {
    using Payload = sos::room::Envelope;

    switch (envelope.payload_case()) {
        case Payload::kTokenValidateRequest:
            handleTokenValidate(envelope.token_validate_request());
            break;

        case Payload::kSlotReleased:
            handleSlotReleased(envelope.slot_released());
            break;

        case Payload::kGameServerHeartbeat:
            handleGameServerHeartbeat(envelope.game_server_heartbeat());
            break;

        default:
            spdlog::warn("[Room:Internal] Unknown message, payload_case={}",
                         static_cast<int>(envelope.payload_case()));
            break;
    }
}

void GameServerSession::handleTokenValidate(const sos::room::TokenValidateRequest& request) {
    sos::room::Envelope response_envelope;
    auto* response = response_envelope.mutable_token_validate_response();

    try {
        auto token_data = session_store_->validateToken(request.auth_token());

        if (token_data) {
            response->set_valid(true);
            response->set_player_id(token_data->player_id);
            response->set_session_id(token_data->session_id);
            spdlog::info("[Room:Internal] Token validated, player_id={}, session_id={}",
                         token_data->player_id, token_data->session_id);
        } else {
            response->set_valid(false);
            spdlog::warn("[Room:Internal] Token invalid or expired, token={}",
                         request.auth_token());
        }
    } catch (const std::exception& e) {
        response->set_valid(false);
        spdlog::error("[Room:Internal] Token validation failed, error={}", e.what());
    }

    send(response_envelope);
}

void GameServerSession::handleSlotReleased(const sos::room::SlotReleased& request) {
    spdlog::info("[Room:Internal] Slot released, player_id={}, session_id={}",
                 request.player_id(), request.session_id());
    room_manager_->handleSlotReleased(request.player_id(), request.session_id());
}

void GameServerSession::handleGameServerHeartbeat(const sos::room::GameServerHeartbeat& request) {
    server_id_ = request.server_id();

    try {
        session_store_->updateGameServerHeartbeat(request.server_id());
    } catch (const std::exception& e) {
        spdlog::error("[Room:Internal] Failed to update heartbeat, server_id={}, error={}",
                      request.server_id(), e.what());
    }

    spdlog::debug("[Room:Internal] Heartbeat, server_id={}, active_sessions={}",
                  request.server_id(), request.active_sessions());
}

void GameServerSession::close() {
    if (closed_) return;
    closed_ = true;

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    if (!server_id_.empty()) {
        spdlog::warn("[Room:Internal] Game server disconnected, server_id={}", server_id_);
        room_manager_->handleGameServerDisconnect();
    }
}

} // namespace sos
