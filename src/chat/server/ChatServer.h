#pragma once

#include <boost/asio.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

namespace sos {

class ChannelManager;
class RateLimiter;

class ChatServer {
public:
    ChatServer(boost::asio::io_context& io_context,
               uint16_t port,
               std::shared_ptr<ChannelManager> channel_manager,
               RateLimiter* rate_limiter,
               std::chrono::seconds heartbeat_timeout);

    void start();
    void stop();

private:
    void doAccept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<ChannelManager> channel_manager_;
    RateLimiter* rate_limiter_;
    std::chrono::seconds heartbeat_timeout_;
};

} // namespace sos
