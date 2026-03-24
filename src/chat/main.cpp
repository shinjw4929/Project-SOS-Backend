#include "util/Logger.h"
#include "util/Config.h"
#include "redis/RedisClient.h"
#include "ratelimit/RateLimiter.h"
#include "server/ChatServer.h"
#include "internal/InternalChannel.h"
#include "channel/ChannelManager.h"

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdint>

int main(int argc, char* argv[]) {
    sos::Logger::init("chat");

    // 기본값
    uint16_t chat_port = 8082;
    uint16_t internal_port = 8083;
    std::string redis_host = "127.0.0.1";
    uint16_t redis_port = 6379;
    std::string redis_password;
    uint32_t heartbeat_timeout_seconds = 90;
    uint32_t rate_limit_max = 10;
    uint32_t rate_limit_window_seconds = 5;
    uint32_t max_message_length = 200;
    uint32_t history_size = 20;
    uint32_t session_ttl_seconds = 7200;

    std::string config_path = (argc > 1) ? argv[1] : "config/server_config.json";
    try {
        sos::Config config(config_path);
        chat_port = config.chatPort();
        internal_port = config.chatInternalPort();
        redis_host = config.redisHost();
        redis_port = config.redisPort();
        redis_password = config.redisPassword();
        heartbeat_timeout_seconds = config.chatHeartbeatTimeoutSeconds();
        rate_limit_max = config.chatRateLimitMax();
        rate_limit_window_seconds = config.chatRateLimitWindowSeconds();
        max_message_length = config.chatMaxMessageLength();
        history_size = config.chatHistorySize();
        session_ttl_seconds = config.chatSessionTtlSeconds();
    } catch (const std::exception&) {
        spdlog::info("[Chat] Config file not found, using defaults");
    }

    // Redis 연결
    sos::RedisClient redis(redis_host, redis_port, redis_password);
    spdlog::info("[Chat] Connected to Redis, host={}, port={}", redis_host, redis_port);

    // Rate Limiter (채팅: player_id 기반, 5초 10회)
    sos::RateLimiter rate_limiter(
        redis, rate_limit_max,
        std::chrono::seconds(rate_limit_window_seconds),
        "chat:rate:");

    boost::asio::io_context io_context;

    // Channel Manager
    auto channel_manager = std::make_shared<sos::ChannelManager>(
        max_message_length, history_size,
        std::chrono::seconds(session_ttl_seconds), &redis);

    // 클라이언트 TCP 서버 (:8082)
    sos::ChatServer server(io_context, chat_port, channel_manager,
                           &rate_limiter,
                           std::chrono::seconds(heartbeat_timeout_seconds));

    // 내부 TCP 채널 (:8083) — Room Server 세션 생성/종료
    sos::InternalChannel internal_channel(io_context, internal_port, channel_manager);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code, int signal_number) {
        spdlog::info("[Chat] Signal {} received, shutting down...", signal_number);
        server.stop();
        internal_channel.stop();
    });

    server.start();
    internal_channel.start();
    spdlog::info("[Chat] Chat Server started, client_port={}, internal_port={}, "
                 "max_message_length={}, history_size={}",
                 chat_port, internal_port, max_message_length, history_size);

    io_context.run();

    spdlog::info("[Chat] Chat Server stopped");
    return 0;
}
