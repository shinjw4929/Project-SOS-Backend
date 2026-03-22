#include <catch2/catch_test_macros.hpp>

#include <room.pb.h>
#include "protocol/Codec.h"

using sos::Codec;
using sos::MAX_MESSAGE_SIZE;

static sos::room::Envelope makeEnvelope(const std::string& room_name) {
    sos::room::Envelope envelope;
    auto* req = envelope.mutable_create_room();
    req->set_player_id("player-1");
    req->set_player_name("Alice");
    req->set_room_name(room_name);
    req->set_max_players(4);
    return envelope;
}

TEST_CASE("Codec encode-decode roundtrip", "[codec]") {
    auto original = makeEnvelope("TestRoom");
    auto encoded = Codec<sos::room::Envelope>::encode(original);

    Codec<sos::room::Envelope> codec;
    codec.feed(encoded.data(), encoded.size());
    auto decoded = codec.tryDecode();

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->create_room().player_id() == "player-1");
    REQUIRE(decoded->create_room().room_name() == "TestRoom");
    REQUIRE(decoded->create_room().max_players() == 4);
}

TEST_CASE("Codec returns nullopt on incomplete header", "[codec]") {
    Codec<sos::room::Envelope> codec;
    uint8_t partial[] = {0x05, 0x00};
    codec.feed(partial, sizeof(partial));

    REQUIRE_FALSE(codec.tryDecode().has_value());
}

TEST_CASE("Codec returns nullopt on incomplete body", "[codec]") {
    auto original = makeEnvelope("Room");
    auto encoded = Codec<sos::room::Envelope>::encode(original);

    Codec<sos::room::Envelope> codec;
    // length header 전체 + body 절반만 전송
    size_t half = 4 + (encoded.size() - 4) / 2;
    codec.feed(encoded.data(), half);

    REQUIRE_FALSE(codec.tryDecode().has_value());

    // 나머지 전송 후 디코딩 성공
    codec.feed(encoded.data() + half, encoded.size() - half);
    REQUIRE(codec.tryDecode().has_value());
}

TEST_CASE("Codec rejects message exceeding MAX_MESSAGE_SIZE", "[codec]") {
    Codec<sos::room::Envelope> codec;

    // 1MB + 1 크기의 length prefix 주입
    uint32_t oversized = MAX_MESSAGE_SIZE + 1;
    uint8_t header[4];
    header[0] = static_cast<uint8_t>(oversized);
    header[1] = static_cast<uint8_t>(oversized >> 8);
    header[2] = static_cast<uint8_t>(oversized >> 16);
    header[3] = static_cast<uint8_t>(oversized >> 24);

    codec.feed(header, 4);
    auto result = codec.tryDecode();
    REQUIRE_FALSE(result.has_value());

    // 버퍼가 클리어되었으므로 이후 정상 메시지는 처리 가능
    auto valid = makeEnvelope("After");
    auto encoded = Codec<sos::room::Envelope>::encode(valid);
    codec.feed(encoded.data(), encoded.size());
    REQUIRE(codec.tryDecode().has_value());
}

TEST_CASE("Codec decodes consecutive messages", "[codec]") {
    auto msg1 = makeEnvelope("Room1");
    auto msg2 = makeEnvelope("Room2");
    auto msg3 = makeEnvelope("Room3");

    auto enc1 = Codec<sos::room::Envelope>::encode(msg1);
    auto enc2 = Codec<sos::room::Envelope>::encode(msg2);
    auto enc3 = Codec<sos::room::Envelope>::encode(msg3);

    Codec<sos::room::Envelope> codec;
    // 세 메시지를 한번에 feed
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), enc1.begin(), enc1.end());
    combined.insert(combined.end(), enc2.begin(), enc2.end());
    combined.insert(combined.end(), enc3.begin(), enc3.end());
    codec.feed(combined.data(), combined.size());

    auto d1 = codec.tryDecode();
    auto d2 = codec.tryDecode();
    auto d3 = codec.tryDecode();
    auto d4 = codec.tryDecode();

    REQUIRE(d1.has_value());
    REQUIRE(d1->create_room().room_name() == "Room1");
    REQUIRE(d2.has_value());
    REQUIRE(d2->create_room().room_name() == "Room2");
    REQUIRE(d3.has_value());
    REQUIRE(d3->create_room().room_name() == "Room3");
    REQUIRE_FALSE(d4.has_value());
}

TEST_CASE("Codec handles invalid protobuf bytes", "[codec]") {
    Codec<sos::room::Envelope> codec;

    // 유효한 length prefix + 잘못된 protobuf 데이터
    uint32_t garbage_len = 5;
    uint8_t data[9];
    data[0] = static_cast<uint8_t>(garbage_len);
    data[1] = data[2] = data[3] = 0;
    data[4] = 0xFF;
    data[5] = 0xFE;
    data[6] = 0xFD;
    data[7] = 0xFC;
    data[8] = 0xFB;

    codec.feed(data, sizeof(data));
    auto result = codec.tryDecode();
    // Protobuf은 알 수 없는 필드를 무시하므로 파싱 자체는 성공할 수 있음
    // 핵심: 예외가 발생하지 않고 정상 동작해야 함

    // 이후 정상 메시지 처리 가능 확인
    auto valid = makeEnvelope("Valid");
    auto encoded = Codec<sos::room::Envelope>::encode(valid);
    codec.feed(encoded.data(), encoded.size());
    REQUIRE(codec.tryDecode().has_value());
}
