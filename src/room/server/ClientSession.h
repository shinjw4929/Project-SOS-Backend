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

class RoomManager;

class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    ClientSession(boost::asio::ip::tcp::socket socket,
                  std::shared_ptr<RoomManager> room_manager);
    ~ClientSession();

    void start();
    void send(const sos::room::Envelope& envelope);

    const std::string& playerId() const { return player_id_; }
    void setPlayerId(const std::string& player_id) { player_id_ = player_id; }

    std::string remoteAddress() const;

private:
    void doRead();
    void doWrite();
    void processMessage(const sos::room::Envelope& envelope);
    void close();

    boost::asio::ip::tcp::socket socket_;
    std::shared_ptr<RoomManager> room_manager_;
    Codec<sos::room::Envelope> codec_;

    std::string player_id_;
    bool closed_ = false;

    std::array<uint8_t, 8192> read_buffer_{};
    std::deque<std::vector<uint8_t>> write_queue_;
};

} // namespace sos
