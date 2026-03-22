#include "RoomManager.h"
#include "Room.h"
#include "server/ClientSession.h"
#include "redis/SessionStore.h"
#include "util/UuidGenerator.h"

#include <spdlog/spdlog.h>

namespace sos {

RoomManager::RoomManager(uint32_t max_rooms, uint32_t max_players_per_room,
                         std::shared_ptr<SessionStore> session_store,
                         std::string game_server_host, uint16_t game_server_port)
    : max_rooms_(max_rooms)
    , max_players_per_room_(max_players_per_room)
    , session_store_(std::move(session_store))
    , game_server_host_(std::move(game_server_host))
    , game_server_port_(game_server_port)
{
}

void RoomManager::registerSession(const std::string& player_id,
                                   std::shared_ptr<ClientSession> session) {
    sessions_[player_id] = session;
}

void RoomManager::unregisterSession(const std::string& player_id) {
    sessions_.erase(player_id);
}

// ============================================================
// Message Handlers
// ============================================================

void RoomManager::handleCreateRoom(const sos::room::CreateRoomRequest& request,
                                    std::shared_ptr<ClientSession> session) {
    const auto& player_id = request.player_id();
    const auto& player_name = request.player_name();
    const auto& room_name = request.room_name();
    uint32_t max_players = request.max_players();

    // 다른 세션에서 동일 player_id 사용 중
    if (auto it = sessions_.find(player_id); it != sessions_.end()) {
        if (auto existing = it->second.lock(); existing && existing != session) {
            sendRejectTo(session, sos::room::RejectResponse::DUPLICATE_PLAYER,
                        "Player ID already connected");
            return;
        }
    }

    // 이 세션에 이미 다른 player_id가 설정된 경우
    if (!session->playerId().empty() && session->playerId() != player_id) {
        sendRejectTo(session, sos::room::RejectResponse::INVALID_REQUEST,
                    "Session already has a different player ID");
        return;
    }

    if (player_to_room_.contains(player_id)) {
        sendRejectTo(session, sos::room::RejectResponse::ALREADY_IN_ROOM,
                    "Already in a room");
        return;
    }

    if (rooms_.size() >= max_rooms_) {
        sendRejectTo(session, sos::room::RejectResponse::INVALID_REQUEST,
                    "Maximum room count reached");
        return;
    }

    if (max_players < 1 || max_players > max_players_per_room_) {
        sendRejectTo(session, sos::room::RejectResponse::INVALID_REQUEST,
                    "Invalid max_players value (1-" + std::to_string(max_players_per_room_) + ")");
        return;
    }

    auto room_id = generateUuid();
    auto room = std::make_shared<Room>(room_id, room_name, player_id, player_name, max_players);

    rooms_[room_id] = room;
    player_to_room_[player_id] = room_id;
    session->setPlayerId(player_id);
    sessions_[player_id] = session;

    sos::room::Envelope envelope;
    auto* response = envelope.mutable_create_room_response();
    response->set_success(true);
    *response->mutable_room() = room->toRoomInfo();
    session->send(envelope);

    spdlog::info("[Room] Room created, room_id={}, room_name={}, host={}",
                 room_id, room_name, player_id);
}

void RoomManager::handleJoinRoom(const sos::room::JoinRoomRequest& request,
                                  std::shared_ptr<ClientSession> session) {
    const auto& player_id = request.player_id();
    const auto& player_name = request.player_name();
    const auto& room_id = request.room_id();

    // 다른 세션에서 동일 player_id 사용 중
    if (auto it = sessions_.find(player_id); it != sessions_.end()) {
        if (auto existing = it->second.lock(); existing && existing != session) {
            sendRejectTo(session, sos::room::RejectResponse::DUPLICATE_PLAYER,
                        "Player ID already connected");
            return;
        }
    }

    if (!session->playerId().empty() && session->playerId() != player_id) {
        sendRejectTo(session, sos::room::RejectResponse::INVALID_REQUEST,
                    "Session already has a different player ID");
        return;
    }

    if (player_to_room_.contains(player_id)) {
        sendRejectTo(session, sos::room::RejectResponse::ALREADY_IN_ROOM,
                    "Already in a room");
        return;
    }

    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        sendRejectTo(session, sos::room::RejectResponse::ROOM_NOT_FOUND,
                    "Room not found");
        return;
    }

    auto& room = room_it->second;

    if (room->state() != sos::room::ROOM_WAITING) {
        sendRejectTo(session, sos::room::RejectResponse::INVALID_REQUEST,
                    "Room is already in game");
        return;
    }

    if (room->isFull()) {
        sendRejectTo(session, sos::room::RejectResponse::ROOM_FULL,
                    "Room is full");
        return;
    }

    room->addPlayer(player_id, player_name);
    player_to_room_[player_id] = room_id;
    session->setPlayerId(player_id);
    sessions_[player_id] = session;

    // JoinRoomResponse -> 참가자
    sos::room::Envelope join_envelope;
    auto* response = join_envelope.mutable_join_room_response();
    response->set_success(true);
    *response->mutable_room() = room->toRoomInfo();
    session->send(join_envelope);

    // RoomUpdate -> 기존 멤버 (참가자 제외)
    sos::room::Envelope update_envelope;
    *update_envelope.mutable_room_update()->mutable_room() = room->toRoomInfo();
    broadcastToRoom(*room, update_envelope, player_id);

    spdlog::info("[Room] Player joined, player_id={}, room_id={}, players={}/{}",
                 player_id, room_id, room->playerCount(), room->maxPlayers());
}

void RoomManager::handleLeaveRoom(const std::string& player_id) {
    auto player_it = player_to_room_.find(player_id);
    if (player_it == player_to_room_.end()) return;

    auto room_id = player_it->second;
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        player_to_room_.erase(player_it);
        return;
    }

    auto room = room_it->second; // shared_ptr 복사 (수명 보장)
    bool was_host = room->isHost(player_id);

    room->removePlayer(player_id);
    player_to_room_.erase(player_id);

    if (was_host) {
        removeRoom(room_id, player_id);
        spdlog::info("[Room] Host left, room removed, room_id={}, host={}", room_id, player_id);
    } else {
        if (room->playerCount() > 0) {
            sos::room::Envelope update_envelope;
            *update_envelope.mutable_room_update()->mutable_room() = room->toRoomInfo();
            broadcastToRoom(*room, update_envelope);
        }
        spdlog::info("[Room] Player left, player_id={}, room_id={}", player_id, room_id);
    }
}

void RoomManager::handleToggleReady(const std::string& player_id) {
    auto player_it = player_to_room_.find(player_id);
    if (player_it == player_to_room_.end()) return;

    auto room_it = rooms_.find(player_it->second);
    if (room_it == rooms_.end()) return;

    auto& room = room_it->second;

    if (room->isHost(player_id)) return;

    room->toggleReady(player_id);

    sos::room::Envelope update_envelope;
    *update_envelope.mutable_room_update()->mutable_room() = room->toRoomInfo();
    broadcastToRoom(*room, update_envelope);

    spdlog::debug("[Room] Player toggled ready, player_id={}, room_id={}",
                  player_id, player_it->second);
}

void RoomManager::handleStartGame(const std::string& player_id) {
    auto player_it = player_to_room_.find(player_id);
    if (player_it == player_to_room_.end()) return;

    auto room_it = rooms_.find(player_it->second);
    if (room_it == rooms_.end()) return;

    auto& room = room_it->second;

    if (!room->isHost(player_id)) {
        sos::room::Envelope reject_env;
        auto* reject = reject_env.mutable_reject();
        reject->set_reason(sos::room::RejectResponse::NOT_HOST);
        reject->set_message("Only host can start the game");
        sendTo(player_id, reject_env);
        return;
    }

    if (!room->canStart()) {
        sos::room::Envelope reject_env;
        auto* reject = reject_env.mutable_reject();
        reject->set_reason(sos::room::RejectResponse::NOT_ALL_READY);
        reject->set_message("Not all players are ready");
        sendTo(player_id, reject_env);
        return;
    }

    room->setState(sos::room::ROOM_IN_GAME);
    auto session_id = generateUuid();
    room->setSessionId(session_id);

    // Redis에 게임 세션 등록
    if (session_store_) {
        try {
            session_store_->registerGameSession(session_id);
        } catch (const std::exception& e) {
            spdlog::error("[Room] Failed to register game session, error={}", e.what());
        }
    }

    // 각 플레이어에게 개별 토큰 발급 후 GameStart 전송
    for (const auto& pid : room->playerIds()) {
        std::string auth_token;
        if (session_store_) {
            try {
                auth_token = session_store_->createToken(pid, session_id);
            } catch (const std::exception& e) {
                spdlog::error("[Room] Token creation failed, player={}, error={}", pid, e.what());
                auth_token = generateUuid();
            }
        } else {
            auth_token = generateUuid();
        }

        sos::room::Envelope env;
        auto* game_start = env.mutable_game_start();
        game_start->set_session_id(session_id);
        game_start->set_auth_token(auth_token);
        game_start->set_game_server_host(game_server_host_);
        game_start->set_game_server_port(game_server_port_);

        sendTo(pid, env);
    }

    spdlog::info("[Room] Game started, room_id={}, session_id={}, players={}",
                 player_it->second, session_id, room->playerCount());
}

void RoomManager::handleRoomListRequest(const sos::room::RoomListRequest& request,
                                         std::shared_ptr<ClientSession> session) {
    uint32_t page = request.page();
    uint32_t page_size = request.page_size();
    if (page_size == 0) page_size = 20;
    if (page_size > 100) page_size = 100;

    std::vector<std::shared_ptr<Room>> waiting_rooms;
    for (const auto& [id, room] : rooms_) {
        if (room->state() == sos::room::ROOM_WAITING) {
            waiting_rooms.push_back(room);
        }
    }

    uint32_t total = static_cast<uint32_t>(waiting_rooms.size());
    uint32_t offset = page * page_size;

    sos::room::Envelope envelope;
    auto* response = envelope.mutable_room_list_response();
    response->set_total_rooms(total);

    for (uint32_t i = offset; i < total && i < offset + page_size; ++i) {
        *response->add_rooms() = waiting_rooms[i]->toRoomSummary();
    }

    session->send(envelope);
}

void RoomManager::handleDisconnect(const std::string& player_id) {
    handleLeaveRoom(player_id);
    unregisterSession(player_id);
    spdlog::info("[Room] Player disconnected, player_id={}", player_id);
}

void RoomManager::handleSlotReleased(const std::string& player_id,
                                      const std::string& session_id) {
    auto player_it = player_to_room_.find(player_id);
    if (player_it == player_to_room_.end()) return;

    auto room_id = player_it->second;
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        player_to_room_.erase(player_id);
        sessions_.erase(player_id);
        return;
    }

    auto& room = room_it->second;

    // session_id가 제공된 경우 일치 여부 검증
    if (!session_id.empty() && room->sessionId() != session_id) {
        spdlog::warn("[Room] SlotReleased session mismatch, player={}, expected={}, got={}",
                     player_id, room->sessionId(), session_id);
        return;
    }

    room->removePlayer(player_id);
    player_to_room_.erase(player_id);
    sessions_.erase(player_id);

    spdlog::info("[Room] Slot released, player_id={}, room_id={}, remaining={}",
                 player_id, room_id, room->playerCount());

    if (room->playerCount() == 0) {
        if (session_store_) {
            try {
                session_store_->unregisterGameSession(room->sessionId());
            } catch (const std::exception& e) {
                spdlog::error("[Room] Failed to unregister game session, error={}", e.what());
            }
        }
        rooms_.erase(room_it);
        spdlog::info("[Room] Game ended, room removed, room_id={}, session_id={}",
                     room_id, session_id);
    }
}

void RoomManager::handleGameServerDisconnect() {
    std::vector<std::string> in_game_room_ids;
    for (const auto& [room_id, room] : rooms_) {
        if (room->state() == sos::room::ROOM_IN_GAME) {
            in_game_room_ids.push_back(room_id);
        }
    }

    for (const auto& room_id : in_game_room_ids) {
        auto room_it = rooms_.find(room_id);
        if (room_it == rooms_.end()) continue;

        auto& room = room_it->second;

        sos::room::Envelope envelope;
        auto* reject = envelope.mutable_reject();
        reject->set_reason(sos::room::RejectResponse::ROOM_CLOSED);
        reject->set_message("Game server disconnected");
        broadcastToRoom(*room, envelope);

        if (session_store_) {
            try {
                session_store_->unregisterGameSession(room->sessionId());
            } catch (const std::exception& e) {
                spdlog::error("[Room] Failed to unregister game session, error={}", e.what());
            }
        }

        for (const auto& pid : room->playerIds()) {
            player_to_room_.erase(pid);
        }

        rooms_.erase(room_it);
    }

    if (!in_game_room_ids.empty()) {
        spdlog::warn("[Room] Game server disconnected, cleaned up {} rooms",
                     in_game_room_ids.size());
    }
}

size_t RoomManager::roomCount() const {
    return rooms_.size();
}

// ============================================================
// Internal Helpers
// ============================================================

void RoomManager::sendTo(const std::string& player_id,
                          const sos::room::Envelope& envelope) {
    auto it = sessions_.find(player_id);
    if (it == sessions_.end()) return;

    if (auto session = it->second.lock()) {
        session->send(envelope);
    }
}

void RoomManager::sendRejectTo(const std::shared_ptr<ClientSession>& session,
                                sos::room::RejectResponse_RejectReason reason,
                                const std::string& message) {
    sos::room::Envelope envelope;
    auto* reject = envelope.mutable_reject();
    reject->set_reason(reason);
    reject->set_message(message);
    session->send(envelope);
}

void RoomManager::broadcastToRoom(const Room& room,
                                   const sos::room::Envelope& envelope) {
    for (const auto& player_id : room.playerIds()) {
        sendTo(player_id, envelope);
    }
}

void RoomManager::broadcastToRoom(const Room& room,
                                   const sos::room::Envelope& envelope,
                                   const std::string& exclude_player_id) {
    for (const auto& player_id : room.playerIds()) {
        if (player_id != exclude_player_id) {
            sendTo(player_id, envelope);
        }
    }
}

void RoomManager::removeRoom(const std::string& room_id,
                              const std::string& exclude_player_id) {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return;

    auto room = it->second; // shared_ptr 복사 (수명 보장)

    sos::room::Envelope envelope;
    auto* reject = envelope.mutable_reject();
    reject->set_reason(sos::room::RejectResponse::ROOM_CLOSED);
    reject->set_message("Room closed by host");
    broadcastToRoom(*room, envelope, exclude_player_id);

    for (const auto& player_id : room->playerIds()) {
        player_to_room_.erase(player_id);
    }

    rooms_.erase(it);
}

} // namespace sos
