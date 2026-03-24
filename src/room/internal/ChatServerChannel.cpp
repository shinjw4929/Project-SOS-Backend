#include "ChatServerChannel.h"

#include <spdlog/spdlog.h>

namespace sos {

ChatServerChannel::ChatServerChannel(boost::asio::io_context& io_context,
                                     const std::string& host, uint16_t port)
    : io_context_(io_context)
    , socket_(io_context)
    , reconnect_timer_(io_context)
    , host_(host)
    , port_(port)
{
}

void ChatServerChannel::start() {
    doConnect();
}

void ChatServerChannel::stop() {
    stopped_ = true;
    reconnect_timer_.cancel();
    close();
}

void ChatServerChannel::sendSessionCreated(
    const std::string& session_id,
    const std::vector<std::pair<std::string, std::string>>& players) {
    sos::chat::ChatEnvelope envelope;
    auto* created = envelope.mutable_session_created();
    created->set_session_id(session_id);

    for (const auto& [player_id, player_name] : players) {
        auto* player = created->add_players();
        player->set_player_id(player_id);
        player->set_player_name(player_name);
    }

    if (connected_) {
        send(envelope);
    } else {
        spdlog::warn("[Room:Chat] Not connected to chat server, dropping SessionCreated, "
                     "session_id={}", session_id);
    }
}

void ChatServerChannel::sendSessionEnded(const std::string& session_id) {
    sos::chat::ChatEnvelope envelope;
    envelope.mutable_session_ended()->set_session_id(session_id);

    if (connected_) {
        send(envelope);
    } else {
        spdlog::warn("[Room:Chat] Not connected to chat server, dropping SessionEnded, "
                     "session_id={}", session_id);
    }
}

// ============================================================
// Connection Management
// ============================================================

void ChatServerChannel::doConnect() {
    if (stopped_) return;

    auto endpoint = boost::asio::ip::tcp::endpoint(
        boost::asio::ip::make_address(host_), port_);

    auto self = shared_from_this();
    socket_.async_connect(endpoint,
        [this, self](boost::system::error_code ec) {
            if (ec) {
                spdlog::warn("[Room:Chat] Failed to connect to chat server, "
                             "host={}, port={}, error={}", host_, port_, ec.message());
                scheduleReconnect();
                return;
            }

            connected_ = true;
            spdlog::info("[Room:Chat] Connected to chat server, host={}, port={}",
                         host_, port_);
            doRead();
        });
}

void ChatServerChannel::scheduleReconnect() {
    if (stopped_) return;

    reconnect_timer_.expires_after(std::chrono::seconds(5));
    auto self = shared_from_this();
    reconnect_timer_.async_wait([this, self](boost::system::error_code ec) {
        if (ec || stopped_) return;
        // 소켓 재생성
        socket_ = boost::asio::ip::tcp::socket(io_context_);
        doConnect();
    });
}

void ChatServerChannel::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this, self](boost::system::error_code ec, size_t /*bytes_transferred*/) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    spdlog::warn("[Room:Chat] Chat server disconnected, error={}",
                                 ec.message());
                }
                close();
                scheduleReconnect();
                return;
            }
            // 현재 Chat Server -> Room Server 응답은 없지만, 연결 감지를 위해 계속 읽음
            doRead();
        });
}

void ChatServerChannel::doWrite() {
    if (write_queue_.empty() || !connected_) return;

    auto self = shared_from_this();
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),
        [this, self](boost::system::error_code ec, size_t /*bytes_transferred*/) {
            if (ec) {
                spdlog::warn("[Room:Chat] Write error, error={}", ec.message());
                close();
                scheduleReconnect();
                return;
            }

            write_queue_.pop_front();
            if (!write_queue_.empty()) {
                doWrite();
            }
        });
}

void ChatServerChannel::send(const sos::chat::ChatEnvelope& envelope) {
    bool was_idle = write_queue_.empty();
    write_queue_.push_back(Codec<sos::chat::ChatEnvelope>::encode(envelope));
    if (was_idle) {
        doWrite();
    }
}

void ChatServerChannel::close() {
    if (!connected_) return;
    connected_ = false;
    write_queue_.clear();

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
}

} // namespace sos
