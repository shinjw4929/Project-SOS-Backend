#include "GameServerChannel.h"
#include "GameServerSession.h"

#include <spdlog/spdlog.h>

namespace sos {

GameServerChannel::GameServerChannel(boost::asio::io_context& io_context,
                                     uint16_t port,
                                     std::shared_ptr<SessionStore> session_store,
                                     std::shared_ptr<RoomManager> room_manager)
    : io_context_(io_context)
    , acceptor_(io_context, boost::asio::ip::tcp::endpoint(
          boost::asio::ip::address_v4::any(), port))
    , session_store_(std::move(session_store))
    , room_manager_(std::move(room_manager))
{
}

void GameServerChannel::start() {
    spdlog::info("[Room:Internal] Internal channel listening on port {}",
                 acceptor_.local_endpoint().port());
    doAccept();
}

void GameServerChannel::stop() {
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void GameServerChannel::doAccept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    spdlog::error("[Room:Internal] Accept error, error={}", ec.message());
                }
                return;
            }

            auto session = std::make_shared<GameServerSession>(
                std::move(socket), session_store_, room_manager_);
            session->start();

            doAccept();
        });
}

} // namespace sos
