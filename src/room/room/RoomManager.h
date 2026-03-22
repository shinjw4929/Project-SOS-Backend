#pragma once

#include <room.pb.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace sos {

class ClientSession;
class Room;
class SessionStore;

class RoomManager {
public:
    RoomManager(uint32_t max_rooms, uint32_t max_players_per_room,
                std::shared_ptr<SessionStore> session_store,
                std::string game_server_host, uint16_t game_server_port);

    void registerSession(const std::string& player_id, std::shared_ptr<ClientSession> session);
    void unregisterSession(const std::string& player_id);

    void handleCreateRoom(const sos::room::CreateRoomRequest& request,
                          std::shared_ptr<ClientSession> session);
    void handleJoinRoom(const sos::room::JoinRoomRequest& request,
                        std::shared_ptr<ClientSession> session);
    void handleLeaveRoom(const std::string& player_id);
    void handleToggleReady(const std::string& player_id);
    void handleStartGame(const std::string& player_id);
    void handleRoomListRequest(const sos::room::RoomListRequest& request,
                               std::shared_ptr<ClientSession> session);
    void handleDisconnect(const std::string& player_id);
    void handleSlotReleased(const std::string& player_id, const std::string& session_id);
    void handleGameServerDisconnect();

    size_t roomCount() const;

private:
    void sendTo(const std::string& player_id, const sos::room::Envelope& envelope);
    void sendRejectTo(const std::shared_ptr<ClientSession>& session,
                      sos::room::RejectResponse_RejectReason reason,
                      const std::string& message);
    void broadcastToRoom(const Room& room, const sos::room::Envelope& envelope);
    void broadcastToRoom(const Room& room, const sos::room::Envelope& envelope,
                         const std::string& exclude_player_id);
    void removeRoom(const std::string& room_id, const std::string& exclude_player_id);

    uint32_t max_rooms_;
    uint32_t max_players_per_room_;
    std::shared_ptr<SessionStore> session_store_;
    std::string game_server_host_;
    uint16_t game_server_port_;

    std::unordered_map<std::string, std::shared_ptr<Room>> rooms_;
    std::unordered_map<std::string, std::string> player_to_room_;
    std::unordered_map<std::string, std::weak_ptr<ClientSession>> sessions_;
};

} // namespace sos
