# Room Server Redis 연동

> Phase 3 (Step 3-1 ~ 3-3). Room Server 핵심(Phase 2) 완료 후 Redis 기반 세션/토큰 관리, Rate Limiting 통합, 게임 서버 내부 TCP 채널 구축.

---

## 개요

Room Server에 Redis를 연동하여 토큰 발급/검증, IP 기반 요청 제한, 게임 서버 내부 통신 채널을 추가했다. Phase 1에서 구현한 RedisClient/RateLimiter를 실제 서비스 로직에 통합하고, 게임 서버(:7979)와의 내부 TCP 채널(:8081)을 구축했다.

---

## 파일 구조

### 신규 파일

```
src/common/redis/
├── SessionStore.h/.cpp          # 토큰 CRUD + 게임 세션 추적 + 하트비트 관리

src/room/internal/
├── GameServerChannel.h/.cpp     # 내부 TCP acceptor (:8081, localhost only)
└── GameServerSession.h/.cpp     # 게임 서버 연결, 토큰 검증/SlotReleased/하트비트
```

### 수정 파일

```
src/common/redis/RedisClient.h/.cpp     # sadd, srem, scard, exists 추가
src/common/CMakeLists.txt               # SessionStore.cpp 추가
src/room/room/Room.h                    # session_id 필드 추가
src/room/room/RoomManager.h/.cpp        # SessionStore 통합, handleSlotReleased/handleGameServerDisconnect
src/room/server/ClientSession.h/.cpp    # RateLimiter + heartbeat 타임아웃 추가
src/room/server/RoomServer.h/.cpp       # RateLimiter/heartbeat_timeout 전달
src/room/main.cpp                       # Redis/SessionStore/RateLimiter/GameServerChannel 배선
src/room/CMakeLists.txt                 # internal/ 소스 추가
```

---

## Step 3-1: 세션/토큰 관리

### SessionStore

Redis를 통한 토큰 생명주기 관리.

| 메서드 | Redis 연산 | 용도 |
|--------|-----------|------|
| `createToken(player_id, session_id)` | `SETEX token:{uuid} 60 {json}` | 입장 토큰 발급 |
| `validateToken(token)` | `GET + DEL token:{uuid}` | 일회용 검증 |
| `registerGameSession(session_id)` | `SADD active_sessions` | 게임 세션 등록 |
| `unregisterGameSession(session_id)` | `SREM active_sessions` | 게임 세션 해제 |
| `updateGameServerHeartbeat(server_id)` | `SETEX game_server:{id} 90` | 하트비트 TTL 갱신 |
| `isGameServerAlive(server_id)` | `EXISTS game_server:{id}` | 생존 확인 |

토큰 값 형식: `{"player_id":"...", "session_id":"..."}` (JSON)

### RoomManager 변경

`handleStartGame` 수정:
1. `Room::setSessionId(session_id)` 호출하여 방에 세션 ID 저장
2. `SessionStore::registerGameSession` 으로 Redis에 세션 등록
3. 각 플레이어마다 `SessionStore::createToken` 으로 토큰 발급 (Redis 장애 시 UUID fallback)
4. `GameStart` 메시지의 `game_server_host`/`port`를 Config 기반으로 설정

### RedisClient 확장

| 추가 메서드 | Redis 명령 | 반환 |
|------------|-----------|------|
| `sadd(key, member)` | `SADD` | 추가된 수 |
| `srem(key, member)` | `SREM` | 제거된 수 |
| `scard(key)` | `SCARD` | SET 크기 |
| `exists(key)` | `EXISTS` | bool |

---

## Step 3-2: Rate Limiting 통합

### ClientSession 변경

`processMessage()` 진입 시 `RateLimiter::allow(ip)` 검사:
- 초과 시 `RejectResponse(RATE_LIMITED)` 전송 후 연결 종료
- Redis 장애 시 fail-open (요청 허용)

IP는 `start()` 시점에 `remote_ip_`로 캐싱하여 매 요청마다 syscall을 피한다.

기본값: 10초 윈도우 내 20회 요청.

### Heartbeat 타임아웃

`boost::asio::steady_timer` 기반.

- `start()` 시 타이머 시작
- 데이터 수신 시마다 `resetHeartbeatTimer()` 호출
- 타임아웃(기본 30초) 도달 시 세션 종료
- `close()` 시 타이머 취소

`kHeartbeat` 메시지는 별도 처리 없이 타이머 리셋 효과만 발생한다 (doRead에서 처리).

---

## Step 3-3: 내부 TCP 채널 (:8081)

### GameServerChannel

`127.0.0.1:8081`에 바인딩하는 내부 전용 TCP acceptor. 인증 불필요.

### GameServerSession

게임 서버 연결을 관리한다. `Codec<Envelope>` 기반 동일 프레이밍.

| 수신 메시지 | 처리 |
|------------|------|
| `TokenValidateRequest` | SessionStore.validateToken → TokenValidateResponse 응답 |
| `SlotReleased` | RoomManager.handleSlotReleased → 플레이어/방 정리 |
| `GameServerHeartbeat` | SessionStore.updateGameServerHeartbeat → TTL 갱신 |

### 게임 서버 단절 처리

`GameServerSession::close()`에서 `RoomManager::handleGameServerDisconnect()` 호출:
1. `ROOM_IN_GAME` 상태인 모든 방 탐색
2. 각 방의 플레이어에게 `RejectResponse(ROOM_CLOSED)` 전송
3. Redis에서 게임 세션 해제
4. 방 및 플레이어 매핑 삭제

### SlotReleased 처리

`RoomManager::handleSlotReleased(player_id, session_id)`:
1. session_id 일치 검증 (불일치 시 무시)
2. 플레이어를 방에서 제거
3. 방에 플레이어가 0명이면: Redis 세션 해제 + 방 삭제

---

## 설계 결정

### Redis 예외 처리 전략

모든 Redis 호출을 try-catch로 감싸 서버 크래시를 방지한다.
- 토큰 발급 실패 → UUID fallback (Redis 없이도 게임 시작 가능)
- Rate Limit 실패 → fail-open (정상 사용자를 거부하지 않음)
- 하트비트/세션 등록 실패 → 경고 로그 후 계속

### TCP 기반 게임 서버 장애 감지

게임 서버 연결 끊김을 TCP 레벨에서 감지하여 즉시 정리한다.
Redis TTL(`game_server:{serverId}`, 90초)은 보조 메커니즘으로, Phase 5에서 다중 게임 서버 환경의 주기적 헬스체크에 활용 예정.

### 싱글 스레드 모델 유지

Redis 호출은 동기(blocking)이지만 localhost Redis의 응답 시간(< 1ms)이 비동기 전환 비용보다 적으므로, Phase 3에서는 동기 호출을 유지한다.

---

## main.cpp 구조

```
Redis 연결
  └── SessionStore (token_ttl=60s, heartbeat_ttl=90s)
  └── RateLimiter (max=20, window=10s)

io_context
  ├── RoomServer (:8080) — ClientSession (rate_limiter, heartbeat_timeout=30s)
  │     └── RoomManager (session_store, game_server_host/port)
  │
  ├── GameServerChannel (:8081) — GameServerSession
  │     ├── SessionStore (토큰 검증)
  │     └── RoomManager (슬롯 반환, 서버 단절)
  │
  └── signal_set (SIGINT/SIGTERM → graceful stop)
```

---

## Redis 키 구조

| 키 | 타입 | TTL | 용도 |
|-----|------|-----|------|
| `token:{uuid}` | STRING | 60초 | 입장 토큰 (JSON: player_id, session_id) |
| `active_sessions` | SET | - | 진행 중인 게임 세션 ID |
| `game_server:{server_id}` | STRING | 90초 | 게임 서버 하트비트 |
| `rate:{ip}` | STRING | 10초 | Rate Limit 카운터 |

---

## 빌드 체계

### CMake 타겟 변경

```
room-server (EXECUTABLE)
├── main.cpp
├── server/RoomServer.cpp
├── server/ClientSession.cpp
├── room/Room.cpp
├── room/RoomManager.cpp
├── internal/GameServerChannel.cpp     ← 신규
├── internal/GameServerSession.cpp     ← 신규
├── sos_common (STATIC)
│   ├── redis/SessionStore.cpp         ← 신규
│   └── (기존 모듈)
└── Boost::system
```
