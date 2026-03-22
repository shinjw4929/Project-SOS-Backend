#pragma once

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>

namespace sos {

class SessionStore;
class RoomManager;

class GameServerChannel {
public:
    GameServerChannel(boost::asio::io_context& io_context,
                      uint16_t port,
                      std::shared_ptr<SessionStore> session_store,
                      std::shared_ptr<RoomManager> room_manager);

    void start();
    void stop();

private:
    void doAccept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<SessionStore> session_store_;
    std::shared_ptr<RoomManager> room_manager_;
};

} // namespace sos
