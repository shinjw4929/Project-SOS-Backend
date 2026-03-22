#pragma once

#include "protocol/Codec.h"

#include <room.pb.h>

#include <boost/asio.hpp>

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace sos {

class SessionStore;
class RoomManager;

class GameServerSession : public std::enable_shared_from_this<GameServerSession> {
public:
    GameServerSession(boost::asio::ip::tcp::socket socket,
                      std::shared_ptr<SessionStore> session_store,
                      std::shared_ptr<RoomManager> room_manager);
    ~GameServerSession();

    void start();
    void send(const sos::room::Envelope& envelope);

    const std::string& serverId() const { return server_id_; }

private:
    void doRead();
    void doWrite();
    void processMessage(const sos::room::Envelope& envelope);

    void handleTokenValidate(const sos::room::TokenValidateRequest& request);
    void handleSlotReleased(const sos::room::SlotReleased& request);
    void handleGameServerHeartbeat(const sos::room::GameServerHeartbeat& request);

    void close();

    boost::asio::ip::tcp::socket socket_;
    std::shared_ptr<SessionStore> session_store_;
    std::shared_ptr<RoomManager> room_manager_;
    Codec<sos::room::Envelope> codec_;

    std::string server_id_;
    bool closed_ = false;

    std::array<uint8_t, 8192> read_buffer_{};
    std::deque<std::vector<uint8_t>> write_queue_;
};

} // namespace sos
