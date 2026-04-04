#include "util/Logger.h"
#include "util/Config.h"
#include "redis/RedisClient.h"
#include "redis/SessionStore.h"
#include "ratelimit/RateLimiter.h"
#include "server/RoomServer.h"
#include "internal/GameServerChannel.h"
#include "internal/ChatServerChannel.h"
#include "room/RoomManager.h"

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdint>

int main(int argc, char* argv[]) {
    sos::Logger::init("room");

    // 기본값
    uint16_t room_port = 8080;
    uint16_t internal_port = 8081;
    uint32_t max_rooms = 100;
    uint32_t max_players_per_room = 8;
    std::string redis_host = "127.0.0.1";
    uint16_t redis_port = 6379;
    std::string redis_password;
    uint32_t token_ttl_seconds = 60;
    uint32_t heartbeat_timeout_seconds = 30;
    uint32_t game_server_heartbeat_ttl_seconds = 90;
    uint32_t rate_limit_max = 20;
    uint32_t rate_limit_window_seconds = 10;
    std::string game_server_host = "127.0.0.1";
    uint16_t game_server_port = 7979;
    std::string chat_server_host = "127.0.0.1";
    uint16_t chat_server_port = 8083;

    std::string config_path = (argc > 1) ? argv[1] : "config/server_config.json";
    try {
        sos::Config config(config_path);
        room_port = config.roomPort();
        internal_port = config.internalPort();
        max_rooms = config.maxRooms();
        max_players_per_room = config.maxPlayersPerRoom();
        redis_host = config.redisHost();
        redis_port = config.redisPort();
        redis_password = config.redisPassword();
        token_ttl_seconds = config.tokenTtlSeconds();
        heartbeat_timeout_seconds = config.heartbeatTimeoutSeconds();
        game_server_heartbeat_ttl_seconds = config.gameServerHeartbeatTtlSeconds();
        rate_limit_max = config.rateLimitMax();
        rate_limit_window_seconds = config.rateLimitWindowSeconds();
        game_server_host = config.gameServerHost();
        game_server_port = config.gameServerPort();
        chat_server_host = config.chatServerHost();
        chat_server_port = config.chatServerPort();
    } catch (const std::exception&) {
        spdlog::info("[Room] Config file not found, using defaults");
    }

    // Redis 연결
    sos::RedisClient redis(redis_host, redis_port, redis_password);
    spdlog::info("[Room] Connected to Redis, host={}, port={}", redis_host, redis_port);

    // 세션/토큰 저장소
    auto session_store = std::make_shared<sos::SessionStore>(
        redis,
        std::chrono::seconds(token_ttl_seconds),
        std::chrono::seconds(game_server_heartbeat_ttl_seconds));

    // Rate Limiter
    sos::RateLimiter rate_limiter(
        redis, rate_limit_max,
        std::chrono::seconds(rate_limit_window_seconds));

    boost::asio::io_context io_context;

    // Chat Server 채널 (Room -> Chat :8083)
    auto chat_channel = std::make_shared<sos::ChatServerChannel>(
        io_context, chat_server_host, chat_server_port);

    // Room Manager
    auto room_manager = std::make_shared<sos::RoomManager>(
        io_context, max_rooms, max_players_per_room,
        session_store, game_server_host, game_server_port, chat_channel);

    // 클라이언트 TCP 서버 (:8080)
    sos::RoomServer server(io_context, room_port, room_manager,
                           &rate_limiter,
                           std::chrono::seconds(heartbeat_timeout_seconds));

    // 내부 TCP 채널 (:8081) — 게임 서버 토큰 검증/슬롯 반환/하트비트
    sos::GameServerChannel internal_channel(
        io_context, internal_port, session_store, room_manager);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code, int signal_number) {
        spdlog::info("[Room] Signal {} received, shutting down...", signal_number);
        room_manager->stop();
        server.stop();
        internal_channel.stop();
        chat_channel->stop();
    });

    server.start();
    internal_channel.start();
    chat_channel->start();
    spdlog::info("[Room] Room Server started, client_port={}, internal_port={}, "
                 "max_rooms={}, max_players={}",
                 room_port, internal_port, max_rooms, max_players_per_room);

    io_context.run();

    spdlog::info("[Room] Room Server stopped");
    return 0;
}
