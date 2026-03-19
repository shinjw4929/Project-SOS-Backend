#include "Room.h"

#include <algorithm>

namespace sos {

Room::Room(std::string room_id, std::string room_name,
           std::string host_id, std::string host_name, uint32_t max_players)
    : room_id_(std::move(room_id))
    , room_name_(std::move(room_name))
    , host_id_(std::move(host_id))
    , host_name_(std::move(host_name))
    , max_players_(max_players)
    , state_(sos::room::ROOM_WAITING)
{
    players_.push_back({host_id_, host_name_, false, true});
}

bool Room::addPlayer(const std::string& player_id, const std::string& player_name) {
    if (isFull() || hasPlayer(player_id) || state_ != sos::room::ROOM_WAITING) {
        return false;
    }
    players_.push_back({player_id, player_name, false, false});
    return true;
}

bool Room::removePlayer(const std::string& player_id) {
    auto it = std::find_if(players_.begin(), players_.end(),
        [&](const PlayerData& player) { return player.player_id == player_id; });
    if (it == players_.end()) return false;
    players_.erase(it);
    return true;
}

void Room::toggleReady(const std::string& player_id) {
    if (player_id == host_id_) return;

    auto it = std::find_if(players_.begin(), players_.end(),
        [&](const PlayerData& player) { return player.player_id == player_id; });
    if (it != players_.end()) {
        it->is_ready = !it->is_ready;
    }
}

bool Room::isFull() const {
    return players_.size() >= max_players_;
}

bool Room::canStart() const {
    // 호스트 외 전원이 준비 완료여야 시작 가능
    // 호스트 혼자인 경우 vacuously true (솔로 플레이 허용)
    for (const auto& player : players_) {
        if (!player.is_host && !player.is_ready) {
            return false;
        }
    }
    return true;
}

bool Room::isHost(const std::string& player_id) const {
    return player_id == host_id_;
}

bool Room::hasPlayer(const std::string& player_id) const {
    return std::any_of(players_.begin(), players_.end(),
        [&](const PlayerData& player) { return player.player_id == player_id; });
}

uint32_t Room::playerCount() const {
    return static_cast<uint32_t>(players_.size());
}

void Room::setState(sos::room::RoomState state) {
    state_ = state;
}

sos::room::RoomState Room::state() const {
    return state_;
}

sos::room::RoomInfo Room::toRoomInfo() const {
    sos::room::RoomInfo info;
    info.set_room_id(room_id_);
    info.set_room_name(room_name_);
    info.set_host_id(host_id_);
    info.set_max_players(max_players_);
    info.set_state(state_);
    for (const auto& player : players_) {
        auto* player_info = info.add_players();
        player_info->set_player_id(player.player_id);
        player_info->set_player_name(player.player_name);
        player_info->set_is_ready(player.is_ready);
        player_info->set_is_host(player.is_host);
    }
    return info;
}

sos::room::RoomListResponse_RoomSummary Room::toRoomSummary() const {
    sos::room::RoomListResponse_RoomSummary summary;
    summary.set_room_id(room_id_);
    summary.set_room_name(room_name_);
    summary.set_host_name(host_name_);
    summary.set_current_players(playerCount());
    summary.set_max_players(max_players_);
    summary.set_state(state_);
    return summary;
}

std::vector<std::string> Room::playerIds() const {
    std::vector<std::string> ids;
    ids.reserve(players_.size());
    for (const auto& player : players_) {
        ids.push_back(player.player_id);
    }
    return ids;
}

} // namespace sos
