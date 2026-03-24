#pragma once

#include "protocol/Codec.h"

#include <chat.pb.h>

#include <boost/asio.hpp>

#include <array>
#include <deque>
#include <memory>
#include <vector>

namespace sos {

class ChannelManager;

class InternalSession : public std::enable_shared_from_this<InternalSession> {
public:
    InternalSession(boost::asio::ip::tcp::socket socket,
                    std::shared_ptr<ChannelManager> channel_manager);
    ~InternalSession();

    void start();

private:
    void doRead();
    void processMessage(const sos::chat::ChatEnvelope& envelope);
    void close();

    boost::asio::ip::tcp::socket socket_;
    std::shared_ptr<ChannelManager> channel_manager_;
    Codec<sos::chat::ChatEnvelope> codec_;

    bool closed_ = false;
    std::array<uint8_t, 8192> read_buffer_{};
};

} // namespace sos
