#include <catch2/catch_test_macros.hpp>

#include "util/UuidGenerator.h"

#include <set>
#include <string>

TEST_CASE("UUID format: 36 chars with hyphens at correct positions", "[uuid]") {
    auto uuid = sos::generateUuid();

    REQUIRE(uuid.size() == 36);
    REQUIRE(uuid[8] == '-');
    REQUIRE(uuid[13] == '-');
    REQUIRE(uuid[18] == '-');
    REQUIRE(uuid[23] == '-');

    // 하이픈 외 문자는 모두 16진수
    for (size_t i = 0; i < uuid.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        REQUIRE(std::isxdigit(static_cast<unsigned char>(uuid[i])));
    }
}

TEST_CASE("UUID version 4 bit pattern", "[uuid]") {
    auto uuid = sos::generateUuid();
    // 14번째 문자(인덱스 14)가 '4' (version 4)
    REQUIRE(uuid[14] == '4');
}

TEST_CASE("UUID variant bit pattern (RFC 4122)", "[uuid]") {
    auto uuid = sos::generateUuid();
    // 19번째 문자(인덱스 19)가 8, 9, a, b 중 하나 (variant = 10xx)
    char variant_char = uuid[19];
    REQUIRE((variant_char == '8' || variant_char == '9'
          || variant_char == 'a' || variant_char == 'b'));
}

TEST_CASE("UUID uniqueness across multiple generations", "[uuid]") {
    constexpr int count = 1000;
    std::set<std::string> uuids;
    for (int i = 0; i < count; ++i) {
        uuids.insert(sos::generateUuid());
    }
    REQUIRE(uuids.size() == count);
}
