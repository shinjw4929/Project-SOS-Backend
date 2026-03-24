#include "ChatServer.h"
#include "ChatSession.h"

#include <spdlog/spdlog.h>

namespace sos {

ChatServer::ChatServer(boost::asio::io_context& io_context,
                       uint16_t port,
                       std::shared_ptr<ChannelManager> channel_manager,
                       RateLimiter* rate_limiter,
                       std::chrono::seconds heartbeat_timeout)
    : io_context_(io_context)
    , acceptor_(io_context, boost::asio::ip::tcp::endpoint(
          boost::asio::ip::tcp::v4(), port))
    , channel_manager_(std::move(channel_manager))
    , rate_limiter_(rate_limiter)
    , heartbeat_timeout_(heartbeat_timeout)
{
}

void ChatServer::start() {
    spdlog::info("[Chat] Server listening on port {}",
                 acceptor_.local_endpoint().port());
    doAccept();
}

void ChatServer::stop() {
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void ChatServer::doAccept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    spdlog::error("[Chat] Accept error, error={}", ec.message());
                }
                return;
            }

            auto session = std::make_shared<ChatSession>(
                std::move(socket), channel_manager_, rate_limiter_, heartbeat_timeout_);
            session->start();

            doAccept();
        });
}

} // namespace sos
