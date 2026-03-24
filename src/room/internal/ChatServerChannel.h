#pragma once

#include "protocol/Codec.h"

#include <chat.pb.h>

#include <boost/asio.hpp>

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace sos {

// Room Server -> Chat Server TCP 클라이언트
// Chat Server의 내부 포트(:8083)에 연결하여 SessionCreated/SessionEnded 전송
class ChatServerChannel : public std::enable_shared_from_this<ChatServerChannel> {
public:
    ChatServerChannel(boost::asio::io_context& io_context,
                      const std::string& host, uint16_t port);

    void start();
    void stop();

    void sendSessionCreated(const std::string& session_id,
                            const std::vector<std::pair<std::string, std::string>>& players);
    void sendSessionEnded(const std::string& session_id);

private:
    void doConnect();
    void scheduleReconnect();
    void doRead();
    void doWrite();
    void send(const sos::chat::ChatEnvelope& envelope);
    void close();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer reconnect_timer_;
    std::string host_;
    uint16_t port_;

    Codec<sos::chat::ChatEnvelope> codec_;
    bool connected_ = false;
    bool stopped_ = false;

    std::array<uint8_t, 4096> read_buffer_{};
    std::deque<std::vector<uint8_t>> write_queue_;
};

} // namespace sos
