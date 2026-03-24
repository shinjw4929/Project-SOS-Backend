#pragma once

#include <room.pb.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sos {

struct PlayerData {
    std::string player_id;
    std::string player_name;
    bool is_ready = false;
    bool is_host = false;
};

class Room {
public:
    Room(std::string room_id, std::string room_name,
         std::string host_id, std::string host_name, uint32_t max_players);

    bool addPlayer(const std::string& player_id, const std::string& player_name);
    bool removePlayer(const std::string& player_id);
    void toggleReady(const std::string& player_id);

    bool isFull() const;
    bool canStart() const;
    bool isHost(const std::string& player_id) const;
    bool hasPlayer(const std::string& player_id) const;
    uint32_t playerCount() const;

    void setState(sos::room::RoomState state);
    sos::room::RoomState state() const;

    sos::room::RoomInfo toRoomInfo() const;
    sos::room::RoomListResponse_RoomSummary toRoomSummary() const;

    const std::string& roomId() const { return room_id_; }
    const std::string& roomName() const { return room_name_; }
    const std::string& hostId() const { return host_id_; }
    uint32_t maxPlayers() const { return max_players_; }
    const std::string& sessionId() const { return session_id_; }
    void setSessionId(std::string session_id) { session_id_ = std::move(session_id); }

    std::vector<std::string> playerIds() const;
    const std::vector<PlayerData>& players() const { return players_; }

private:
    std::string room_id_;
    std::string room_name_;
    std::string host_id_;
    std::string host_name_;
    uint32_t max_players_;
    std::string session_id_;
    sos::room::RoomState state_;
    std::vector<PlayerData> players_;
};

} // namespace sos
