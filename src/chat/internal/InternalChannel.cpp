#include "InternalChannel.h"
#include "InternalSession.h"

#include <spdlog/spdlog.h>

namespace sos {

InternalChannel::InternalChannel(boost::asio::io_context& io_context,
                                 uint16_t port,
                                 std::shared_ptr<ChannelManager> channel_manager)
    : io_context_(io_context)
    , acceptor_(io_context, boost::asio::ip::tcp::endpoint(
          boost::asio::ip::address_v4::any(), port))
    , channel_manager_(std::move(channel_manager))
{
}

void InternalChannel::start() {
    spdlog::info("[Chat:Internal] Internal channel listening on port {}",
                 acceptor_.local_endpoint().port());
    doAccept();
}

void InternalChannel::stop() {
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void InternalChannel::doAccept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    spdlog::error("[Chat:Internal] Accept error, error={}", ec.message());
                }
                return;
            }

            auto session = std::make_shared<InternalSession>(
                std::move(socket), channel_manager_);
            session->start();

            doAccept();
        });
}

} // namespace sos
