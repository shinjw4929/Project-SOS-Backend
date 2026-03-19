#include "RoomManager.h"
#include "Room.h"
#include "server/ClientSession.h"
#include "util/UuidGenerator.h"

#include <spdlog/spdlog.h>

namespace sos {

RoomManager::RoomManager(uint32_t max_rooms, uint32_t max_players_per_room)
    : max_rooms_(max_rooms)
    , max_players_per_room_(max_players_per_room)
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

    // 각 플레이어에게 개별 토큰 발급 후 GameStart 전송
    // Phase 2: 토큰은 UUID만 생성 (Redis 저장은 Phase 3)
    for (const auto& pid : room->playerIds()) {
        auto auth_token = generateUuid();

        sos::room::Envelope env;
        auto* game_start = env.mutable_game_start();
        game_start->set_session_id(session_id);
        game_start->set_auth_token(auth_token);
        game_start->set_game_server_host("127.0.0.1");
        game_start->set_game_server_port(7979);

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
