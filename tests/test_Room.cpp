#include <catch2/catch_test_macros.hpp>

#include "room/Room.h"

using sos::Room;

static Room makeRoom(uint32_t max_players = 4) {
    return Room("room-1", "TestRoom", "host-1", "HostName", max_players);
}

// --- 생성 ---

TEST_CASE("Room constructor adds host as first player", "[room]") {
    auto room = makeRoom();

    REQUIRE(room.playerCount() == 1);
    REQUIRE(room.hasPlayer("host-1"));
    REQUIRE(room.isHost("host-1"));
    REQUIRE(room.roomId() == "room-1");
    REQUIRE(room.roomName() == "TestRoom");
    REQUIRE(room.hostId() == "host-1");
    REQUIRE(room.maxPlayers() == 4);
    REQUIRE(room.state() == sos::room::ROOM_WAITING);
}

// --- addPlayer ---

TEST_CASE("Room addPlayer succeeds for new player", "[room]") {
    auto room = makeRoom();

    REQUIRE(room.addPlayer("p2", "Player2"));
    REQUIRE(room.playerCount() == 2);
    REQUIRE(room.hasPlayer("p2"));
}

TEST_CASE("Room addPlayer rejects duplicate player", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");

    REQUIRE_FALSE(room.addPlayer("p2", "Player2"));
    REQUIRE(room.playerCount() == 2);
}

TEST_CASE("Room addPlayer rejects when full", "[room]") {
    auto room = makeRoom(2);
    room.addPlayer("p2", "Player2");

    REQUIRE(room.isFull());
    REQUIRE_FALSE(room.addPlayer("p3", "Player3"));
}

TEST_CASE("Room addPlayer rejects when game in progress", "[room]") {
    auto room = makeRoom();
    room.setState(sos::room::ROOM_IN_GAME);

    REQUIRE_FALSE(room.addPlayer("p2", "Player2"));
}

// --- removePlayer ---

TEST_CASE("Room removePlayer removes existing player", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");

    REQUIRE(room.removePlayer("p2"));
    REQUIRE(room.playerCount() == 1);
    REQUIRE_FALSE(room.hasPlayer("p2"));
}

TEST_CASE("Room removePlayer returns false for unknown player", "[room]") {
    auto room = makeRoom();

    REQUIRE_FALSE(room.removePlayer("nonexistent"));
}

// --- toggleReady ---

TEST_CASE("Room toggleReady ignores host", "[room]") {
    auto room = makeRoom();
    room.toggleReady("host-1");

    // 호스트는 canStart에서 검사하지 않으므로 솔로 여전히 true
    REQUIRE(room.canStart());
}

TEST_CASE("Room toggleReady toggles non-host player", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");

    REQUIRE_FALSE(room.canStart()); // p2 미준비

    room.toggleReady("p2");
    REQUIRE(room.canStart()); // p2 준비

    room.toggleReady("p2");
    REQUIRE_FALSE(room.canStart()); // p2 다시 미준비
}

// --- canStart ---

TEST_CASE("Room canStart with solo host returns true", "[room]") {
    auto room = makeRoom();
    REQUIRE(room.canStart());
}

TEST_CASE("Room canStart with unready player returns false", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");
    room.addPlayer("p3", "Player3");

    REQUIRE_FALSE(room.canStart());
}

TEST_CASE("Room canStart with all players ready returns true", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");
    room.addPlayer("p3", "Player3");

    room.toggleReady("p2");
    room.toggleReady("p3");
    REQUIRE(room.canStart());
}

// --- isFull ---

TEST_CASE("Room isFull reflects capacity", "[room]") {
    auto room = makeRoom(2);

    REQUIRE_FALSE(room.isFull());
    room.addPlayer("p2", "Player2");
    REQUIRE(room.isFull());
}

// --- 상태 전이 ---

TEST_CASE("Room state transition", "[room]") {
    auto room = makeRoom();
    REQUIRE(room.state() == sos::room::ROOM_WAITING);

    room.setState(sos::room::ROOM_IN_GAME);
    REQUIRE(room.state() == sos::room::ROOM_IN_GAME);
}

// --- playerIds ---

TEST_CASE("Room playerIds returns all player IDs", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");
    room.addPlayer("p3", "Player3");

    auto ids = room.playerIds();
    REQUIRE(ids.size() == 3);
    REQUIRE(std::find(ids.begin(), ids.end(), "host-1") != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), "p2") != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), "p3") != ids.end());
}

// --- Protobuf 변환 ---

TEST_CASE("Room toRoomInfo produces correct Protobuf", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");
    room.toggleReady("p2");

    auto info = room.toRoomInfo();
    REQUIRE(info.room_id() == "room-1");
    REQUIRE(info.room_name() == "TestRoom");
    REQUIRE(info.host_id() == "host-1");
    REQUIRE(info.max_players() == 4);
    REQUIRE(info.state() == sos::room::ROOM_WAITING);
    REQUIRE(info.players_size() == 2);

    // 호스트 확인
    const auto& host = info.players(0);
    REQUIRE(host.player_id() == "host-1");
    REQUIRE(host.player_name() == "HostName");
    REQUIRE(host.is_host());
    REQUIRE_FALSE(host.is_ready());

    // 일반 플레이어 확인
    const auto& player = info.players(1);
    REQUIRE(player.player_id() == "p2");
    REQUIRE(player.player_name() == "Player2");
    REQUIRE_FALSE(player.is_host());
    REQUIRE(player.is_ready());
}

TEST_CASE("Room toRoomSummary produces correct Protobuf", "[room]") {
    auto room = makeRoom();
    room.addPlayer("p2", "Player2");

    auto summary = room.toRoomSummary();
    REQUIRE(summary.room_id() == "room-1");
    REQUIRE(summary.room_name() == "TestRoom");
    REQUIRE(summary.host_name() == "HostName");
    REQUIRE(summary.current_players() == 2);
    REQUIRE(summary.max_players() == 4);
    REQUIRE(summary.state() == sos::room::ROOM_WAITING);
}
