# common 라이브러리 구현

> Phase 1 - Step 1-3. CMake 빌드 체계(1-2) 완료 후 공유 라이브러리 구축.

---

## 개요

Room/Chat 서버가 공유하는 `sos_common` 정적 라이브러리에 5개 모듈을 구현했다.

---

## 모듈 구조

```
src/common/
├── protocol/
│   └── Codec.h              # 헤더 온리 템플릿
├── redis/
│   ├── RedisClient.h
│   └── RedisClient.cpp       # pimpl 패턴 (sw::redis++ 래퍼)
├── ratelimit/
│   ├── RateLimiter.h
│   └── RateLimiter.cpp
├── util/
│   ├── Logger.h
│   ├── Logger.cpp
│   ├── Config.h
│   └── Config.cpp
└── CMakeLists.txt
```

---

## 모듈별 설명

### 1. Codec<T> (protocol/Codec.h)

4byte LE 길이 접두사 + Protobuf 바이너리 프레이밍을 처리하는 템플릿 클래스.

```
[4 bytes: payload length (little-endian)] [N bytes: Protobuf binary]
```

- `encode(message)`: Protobuf 직렬화 + 4byte LE 길이 접두사 추가
- `feed(data, size)`: TCP 스트림에서 수신한 원시 바이트를 내부 버퍼에 축적
- `tryDecode()`: 버퍼에서 완전한 메시지 1개를 추출 (불완전하면 `std::nullopt`)
- 1MB 초과 메시지는 프로토콜 에러로 처리 (버퍼 초기화)
- 템플릿 파라미터: `sos::room::Envelope` 또는 `sos::chat::ChatEnvelope`

### 2. Logger (util/Logger.h)

spdlog 기반 로거 초기화. Vector 파싱 정규식과 일치하는 대문자 레벨 포맷.

```
2026-03-18 22:16:50.915 | INFO    | [Room] Room Server starting...
```

- `Logger::init(service_name)` 호출 후 `spdlog::info()`, `spdlog::error()` 등 직접 사용
- 커스텀 플래그 포매터로 대문자 레벨명 7칸 좌측 정렬 (TRACE, DEBUG, INFO, WARNING, ERROR, CRITICAL)

### 3. Config (util/Config.h)

nlohmann/json 기반 JSON 설정 파일 로더.

- `Config(filepath)`: JSON 파일 파싱
- `get<T>(key, default_value)`: 키 조회, 없으면 기본값
- 서버 설정 전용 접근자: `roomPort()`, `internalPort()`, `redisHost()`, `redisPort()`, `redisPassword()`, `maxRooms()`, `maxPlayersPerRoom()`, `tokenTtlSeconds()`, `rateLimitMax()` 등

### 4. RedisClient (redis/RedisClient.h)

redis-plus-plus 래퍼. pimpl 패턴으로 sw::redis++ 헤더를 소비자에게 노출하지 않음.

- `get(key)`, `set(key, value)`, `setex(key, value, ttl)`, `del(key)`, `incr(key)`, `expire(key, ttl)`
- Move-only (복사 금지)

### 5. RateLimiter (ratelimit/RateLimiter.h)

Redis INCR + EXPIRE 기반 슬라이딩 윈도우 카운터.

- `allow(identifier)`: `rate:{identifier}` 키를 INCR하고 윈도우 내 요청 수가 임계값 이하이면 허용
- 첫 요청 시 TTL 설정 (윈도우 시작)

---

## 의존성 추가

vcpkg.json에 `hiredis`, `redis-plus-plus` 추가.

| 패키지 | 버전 | CMake 타겟 |
|--------|------|-----------|
| hiredis | 1.3.0 | `hiredis::hiredis` |
| redis-plus-plus | 1.3.15 | `redis++::redis++` |

---

## 빌드 결과

```
[1/13] Generating C++ from chat.proto
[2/13] Generating C++ from room.proto
[3-10]  sos_common 소스 컴파일 (Logger, Config, RedisClient, RateLimiter, protobuf generated)
[11/13] Linking CXX static library src/common/sos_common.lib
[12/13] Linking CXX executable src/room/room-server.exe
[13/13] Linking CXX executable src/chat/chat-server.exe
```

로그 출력 확인:
```
> build\src\room\room-server.exe
2026-03-18 22:16:50.915 | INFO    | [Room] Room Server starting...

> build\src\chat\chat-server.exe
2026-03-18 22:16:51.391 | INFO    | [Chat] Chat Server starting...
```
