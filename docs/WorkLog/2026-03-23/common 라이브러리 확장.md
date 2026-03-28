# common 라이브러리 확장

> Phase 4 사전 작업. Chat Server 구현에 필요한 공통 라이브러리 확장.

---

## 개요

Chat Server가 기존 common 라이브러리를 재사용하되, 채팅 전용 Rate Limit 키 접두사, Chat Server 설정값, Redis HASH/LIST 연산이 필요하여 확장했다. 기존 Room Server 코드에 대한 하위호환을 유지한다.

---

## RateLimiter: key_prefix 파라미터

### 변경 동기

Room Server는 `rate:{ip}` 키를 사용하고, Chat Server는 `chat:rate:{player_id}` 키를 사용한다. 키 접두사를 설정 가능하게 하여 동일 RateLimiter 클래스를 재사용한다.

### 변경 내용

```cpp
// 변경 전
RateLimiter(RedisClient& redis, int max_requests, std::chrono::seconds window);
// key = "rate:" + identifier

// 변경 후
RateLimiter(RedisClient& redis, int max_requests, std::chrono::seconds window,
            std::string key_prefix = "rate:");
// key = key_prefix + identifier
```

기본값 `"rate:"`이므로 Room Server 코드는 수정 불필요.

### 사용 예시

```cpp
// Room Server (기존): rate:{ip}, 10초 20회
RateLimiter room_limiter(redis, 20, std::chrono::seconds(10));

// Chat Server (신규): chat:rate:{player_id}, 5초 10회
RateLimiter chat_limiter(redis, 10, std::chrono::seconds(5), "chat:rate:");
```

---

## Config: Chat Server 접근자

### 추가 접근자

| 메서드 | 키 | 기본값 | 용도 |
|--------|-----|--------|------|
| `chatPort()` | `chat_port` | 8082 | 클라이언트 TCP 포트 |
| `chatInternalPort()` | `chat_internal_port` | 8083 | 내부 TCP 포트 |
| `chatHeartbeatTimeoutSeconds()` | `chat_heartbeat_timeout_seconds` | 90 | 하트비트 타임아웃 |
| `chatRateLimitMax()` | `chat_rate_limit_max` | 10 | Rate Limit 최대 횟수 |
| `chatRateLimitWindowSeconds()` | `chat_rate_limit_window_seconds` | 5 | Rate Limit 윈도우 |
| `chatMaxMessageLength()` | `chat_max_message_length` | 200 | 최대 메시지 길이 (바이트) |
| `chatHistorySize()` | `chat_history_size` | 20 | 세션 히스토리 보관 개수 |
| `chatSessionTtlSeconds()` | `chat_session_ttl_seconds` | 7200 | 세션 Redis TTL |
| `chatServerHost()` | `chat_server_host` | 127.0.0.1 | Chat Server 호스트 (Room Server용) |
| `chatServerPort()` | `chat_server_port` | 8083 | Chat Server 내부 포트 (Room Server용) |

---

## RedisClient: HASH / LIST 연산

### 추가 동기

Chat Server의 세션 플레이어 매핑(HASH)과 메시지 히스토리(LIST)에 필요하다.

### HASH 연산

| 메서드 | Redis 명령 | 반환 | 용도 |
|--------|-----------|------|------|
| `hset(key, field, value)` | `HSET` | void | 세션에 플레이어 추가 |
| `hget(key, field)` | `HGET` | optional\<string\> | 플레이어 정보 조회 |
| `hgetall(key)` | `HGETALL` | unordered_map | 세션 전체 멤버 조회 |
| `hdel(key, field)` | `HDEL` | bool | 세션에서 플레이어 제거 |

### LIST 연산

| 메서드 | Redis 명령 | 반환 | 용도 |
|--------|-----------|------|------|
| `lpush(key, value)` | `LPUSH` | int64 | 히스토리에 메시지 추가 |
| `ltrim(key, start, stop)` | `LTRIM` | void | 히스토리 크기 제한 |
| `lrange(key, start, stop)` | `LRANGE` | vector\<string\> | 히스토리 조회 |

---

## 수정 파일

```
src/common/ratelimit/RateLimiter.h    # key_prefix_ 멤버 + 생성자 파라미터
src/common/ratelimit/RateLimiter.cpp  # key_prefix_ 사용
src/common/util/Config.h              # Chat 접근자 10개 선언
src/common/util/Config.cpp            # Chat 접근자 10개 구현
src/common/redis/RedisClient.h        # HASH 4개 + LIST 3개 메서드, <unordered_map>/<vector> include
src/common/redis/RedisClient.cpp      # HASH 4개 + LIST 3개 구현 (redis-plus-plus 위임)
```
