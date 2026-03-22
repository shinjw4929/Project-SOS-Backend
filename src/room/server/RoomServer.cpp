#include "RoomServer.h"
#include "ClientSession.h"
#include "room/RoomManager.h"

#include <spdlog/spdlog.h>

namespace sos {

RoomServer::RoomServer(boost::asio::io_context& io_context,
                       uint16_t port,
                       std::shared_ptr<RoomManager> room_manager,
                       RateLimiter* rate_limiter,
                       std::chrono::seconds heartbeat_timeout)
    : io_context_(io_context)
    , acceptor_(io_context, boost::asio::ip::tcp::endpoint(
          boost::asio::ip::tcp::v4(), port))
    , room_manager_(std::move(room_manager))
    , rate_limiter_(rate_limiter)
    , heartbeat_timeout_(heartbeat_timeout)
{
}

void RoomServer::start() {
    spdlog::info("[Room] Server listening on port {}",
                 acceptor_.local_endpoint().port());
    doAccept();
}

void RoomServer::stop() {
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void RoomServer::doAccept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    spdlog::error("[Room] Accept error, error={}", ec.message());
                }
                return;
            }

            auto session = std::make_shared<ClientSession>(
                std::move(socket), room_manager_, rate_limiter_, heartbeat_timeout_);
            session->start();

            doAccept();
        });
}

} // namespace sos
