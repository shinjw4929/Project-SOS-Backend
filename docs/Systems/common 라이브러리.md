# common 라이브러리

`src/common/` — Room Server, Chat Server가 공유하는 STATIC 라이브러리.

---

## 모듈 구조

```
src/common/
├── protocol/
│   └── Codec.h           # 템플릿 — Protobuf 프레이밍/디코딩
├── redis/
│   ├── RedisClient.h/cpp  # Redis 래퍼 (Pimpl)
│   └── SessionStore.h/cpp # 토큰/세션 관리
├── ratelimit/
│   └── RateLimiter.h/cpp  # Redis 기반 Rate Limiting
└── util/
    ├── Logger.h/cpp       # spdlog 초기화 + 커스텀 포매터
    ├── Config.h/cpp       # JSON 설정 + 환경변수 오버라이드
    └── UuidGenerator.h/cpp # RFC 4122 UUID v4
```

---

## Codec\<T\>

**역할**: TCP 스트림에서 Protobuf 메시지를 프레이밍/디코딩.

**프로토콜**: 4바이트 LE 길이 접두사 + Protobuf 직렬화 바이트.

| 메서드 | 설명 |
|--------|------|
| `feed(data, size)` | 수신 바이트를 내부 버퍼에 추가 |
| `tryDecode() → optional<T>` | 완전한 메시지가 있으면 디코딩하여 반환, 없으면 nullopt |
| `encode(message) → vector<uint8_t>` | static. 메시지를 길이 접두사 + 직렬화 바이트로 인코딩 |

- 최대 메시지 크기: 1MB
- 파싱 실패 시 해당 메시지 건너뜀 (silent drop)
- 스레드 안전: 인스턴스 단위 (세션당 1개)

---

## RedisClient

**역할**: redis-plus-plus 래핑. Pimpl 패턴으로 의존성 격리.

| 카테고리 | 메서드 |
|----------|--------|
| String | `get`, `set`, `setex`, `del`, `incr`, `expire`, `exists` |
| Set | `sadd`, `srem`, `scard` |
| Hash | `hset`, `hget`, `hgetall`, `hdel` |
| List | `lpush`, `ltrim`, `lrange` |

- Move-only 시맨틱 (복사 불가)
- 동기 API (블로킹 호출)
- 반환: nullable 값은 `std::optional<T>`

---

## SessionStore

**역할**: Redis 기반 토큰 발급/검증, 게임 세션 관리, 서버 하트비트.

| 메서드 | Redis 키 | 설명 |
|--------|----------|------|
| `createToken(player_id, session_id)` | `token:{uuid}` | 60초 TTL, JSON {player_id, session_id} |
| `validateToken(token)` | `token:{token}` | 조회 후 즉시 삭제 (일회성) |
| `registerGameSession(session_id)` | `active_sessions` | SET에 추가 |
| `unregisterGameSession(session_id)` | `active_sessions` | SET에서 제거 |
| `updateGameServerHeartbeat(server_id)` | `game_server:{server_id}` | 90초 TTL 갱신 |
| `isGameServerAlive(server_id)` | `game_server:{server_id}` | 키 존재 여부 |

- RedisClient 비소유 참조 (`RedisClient&`)
- UUID 생성: `generateUuid()` 사용
- JSON 직렬화: nlohmann/json

---

## RateLimiter

**역할**: Redis 기반 분산 Rate Limiting.

```
RateLimiter(redis, max_requests, window, key_prefix = "rate:")
allow(identifier) → bool
```

**알고리즘**: Redis `INCR` + `EXPIRE` (첫 요청 시 TTL 설정).

| 서버 | key_prefix | max | window |
|------|-----------|-----|--------|
| Room | `rate:` | 20 | 10초 |
| Chat | `chat:rate:` | 10 | 5초 |

---

## Logger

**역할**: spdlog 글로벌 로거 초기화.

```
Logger::init("room")  // 또는 "chat"
```

**출력 형식**: `2026-03-18 10:00:01.100 | INFO    | [Room] message`

- 커스텀 `UppercaseLevelFlag`: 7자 고정폭 레벨 (INFO   , WARNING 등)
- Vector 파싱 정규식과 일치하는 형식
- 멀티스레드 안전 (stdout_color_sink_mt)

---

## Config

**역할**: JSON 설정 파일 로드 + 환경변수 오버라이드.

```
Config config("config/server_config.json");
uint16_t port = config.roomPort();  // 8080
```

**환경변수 오버라이드** (Docker 배포용):

| 환경변수 | JSON 키 |
|---------|---------|
| `REDIS_HOST` | `redis_host` |
| `REDIS_PORT` | `redis_port` |
| `REDIS_PASSWORD` | `redis_password` |
| `CHAT_SERVER_HOST` | `chat_server_host` |
| `CHAT_SERVER_PORT` | `chat_server_port` |

- 제네릭 `get<T>(key, default)` + 이름 있는 accessor 메서드
- 생성 후 읽기 전용 (const accessor)

---

## UuidGenerator

**역할**: RFC 4122 UUID v4 생성.

```
std::string id = generateUuid();  // "550e8400-e29b-41d4-a716-446655440000"
```

- `thread_local std::mt19937` — 스레드당 독립 RNG
- 암호학적 품질 보장 안 됨 (세션 ID 용도)

---

## 모듈 의존성

```
Config ──────────────┐
Logger ──────────────┤ (다른 모듈이 의존)
UuidGenerator ───────┤
                     │
RedisClient ─────────┤
  ├── SessionStore ──┤
  └── RateLimiter ───┘
                     │
Codec<T> ────────────┘ (각 세션이 인스턴스 보유)
```
