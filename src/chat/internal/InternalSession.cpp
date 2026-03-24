#include "InternalSession.h"
#include "channel/ChannelManager.h"

#include <spdlog/spdlog.h>

namespace sos {

InternalSession::InternalSession(boost::asio::ip::tcp::socket socket,
                                 std::shared_ptr<ChannelManager> channel_manager)
    : socket_(std::move(socket))
    , channel_manager_(std::move(channel_manager))
{
}

InternalSession::~InternalSession() {
    close();
}

void InternalSession::start() {
    try {
        spdlog::info("[Chat:Internal] Room server connected, remote={}",
                     socket_.remote_endpoint().address().to_string());
    } catch (...) {
        spdlog::info("[Chat:Internal] Room server connected");
    }
    doRead();
}

void InternalSession::doRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this, self](boost::system::error_code ec, size_t bytes_transferred) {
            if (ec) {
                if (ec != boost::asio::error::eof &&
                    ec != boost::asio::error::operation_aborted) {
                    spdlog::warn("[Chat:Internal] Read error, error={}", ec.message());
                }
                close();
                return;
            }

            codec_.feed(read_buffer_.data(), bytes_transferred);

            while (auto envelope = codec_.tryDecode()) {
                processMessage(*envelope);
            }

            doRead();
        });
}

void InternalSession::processMessage(const sos::chat::ChatEnvelope& envelope) {
    using Payload = sos::chat::ChatEnvelope;

    switch (envelope.payload_case()) {
        case Payload::kSessionCreated:
            channel_manager_->handleSessionCreated(envelope.session_created());
            break;

        case Payload::kSessionEnded:
            channel_manager_->handleSessionEnded(envelope.session_ended());
            break;

        default:
            spdlog::warn("[Chat:Internal] Unknown message, payload_case={}",
                         static_cast<int>(envelope.payload_case()));
            break;
    }
}

void InternalSession::close() {
    if (closed_) return;
    closed_ = true;

    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    spdlog::info("[Chat:Internal] Room server disconnected");
}

} // namespace sos
