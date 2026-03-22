#include <catch2/catch_test_macros.hpp>

#include "util/Config.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

// 임시 JSON 파일을 생성하고 소멸 시 삭제하는 헬퍼
struct TempConfigFile {
    std::string path;

    explicit TempConfigFile(const std::string& json_content)
        : path(std::tmpnam(nullptr) + std::string(".json"))
    {
        std::ofstream out(path);
        out << json_content;
    }

    ~TempConfigFile() {
        std::remove(path.c_str());
    }
};

TEST_CASE("Config parses JSON and returns values", "[config]") {
    TempConfigFile file(R"({
        "room_port": 9090,
        "redis_host": "10.0.0.1",
        "max_rooms": 50
    })");

    sos::Config config(file.path);
    REQUIRE(config.roomPort() == 9090);
    REQUIRE(config.redisHost() == "10.0.0.1");
    REQUIRE(config.maxRooms() == 50);
}

TEST_CASE("Config returns default values for missing keys", "[config]") {
    TempConfigFile file("{}");

    sos::Config config(file.path);
    REQUIRE(config.roomPort() == 8080);
    REQUIRE(config.internalPort() == 8081);
    REQUIRE(config.redisHost() == "127.0.0.1");
    REQUIRE(config.redisPort() == 6379);
    REQUIRE(config.redisPassword().empty());
    REQUIRE(config.maxRooms() == 100);
    REQUIRE(config.maxPlayersPerRoom() == 8);
    REQUIRE(config.tokenTtlSeconds() == 60);
    REQUIRE(config.heartbeatTimeoutSeconds() == 30);
    REQUIRE(config.gameServerHeartbeatTtlSeconds() == 90);
    REQUIRE(config.rateLimitMax() == 20);
    REQUIRE(config.rateLimitWindowSeconds() == 10);
    REQUIRE(config.gameServerHost() == "127.0.0.1");
    REQUIRE(config.gameServerPort() == 7979);
}

TEST_CASE("Config::get returns default for nonexistent key", "[config]") {
    TempConfigFile file(R"({"some_key": 42})");

    sos::Config config(file.path);
    REQUIRE(config.get<int>("missing_key", 999) == 999);
    REQUIRE(config.get<std::string>("missing_str", "fallback") == "fallback");
}

TEST_CASE("Config throws on nonexistent file", "[config]") {
    REQUIRE_THROWS_AS(
        sos::Config("/nonexistent/path/config.json"),
        std::runtime_error
    );
}

TEST_CASE("Config accessor methods override defaults when key exists", "[config]") {
    TempConfigFile file(R"({
        "room_port": 1234,
        "internal_port": 5678,
        "redis_port": 9999,
        "redis_password": "secret",
        "max_players_per_room": 4,
        "token_ttl_seconds": 120,
        "heartbeat_timeout_seconds": 15,
        "game_server_heartbeat_ttl_seconds": 45,
        "rate_limit_max": 50,
        "rate_limit_window_seconds": 30,
        "game_server_host": "192.168.1.1",
        "game_server_port": 8888
    })");

    sos::Config config(file.path);
    REQUIRE(config.roomPort() == 1234);
    REQUIRE(config.internalPort() == 5678);
    REQUIRE(config.redisPort() == 9999);
    REQUIRE(config.redisPassword() == "secret");
    REQUIRE(config.maxPlayersPerRoom() == 4);
    REQUIRE(config.tokenTtlSeconds() == 120);
    REQUIRE(config.heartbeatTimeoutSeconds() == 15);
    REQUIRE(config.gameServerHeartbeatTtlSeconds() == 45);
    REQUIRE(config.rateLimitMax() == 50);
    REQUIRE(config.rateLimitWindowSeconds() == 30);
    REQUIRE(config.gameServerHost() == "192.168.1.1");
    REQUIRE(config.gameServerPort() == 8888);
}
