# Room Server

`src/room/` — 방 관리, 게임 시작, 토큰 발급을 담당하는 TCP 서버.

---

## 모듈 구조

```
src/room/
├── main.cpp                        # 진입점, 초기화 순서
├── server/
│   ├── RoomServer.h/cpp            # TCP acceptor (:8080)
│   └── ClientSession.h/cpp         # 클라이언트 세션
├── room/
│   ├── Room.h/cpp                  # 방 상태/플레이어 관리
│   └── RoomManager.h/cpp           # 핵심 비즈니스 로직
└── internal/
    ├── GameServerChannel.h/cpp     # TCP acceptor (:8081, localhost)
    ├── GameServerSession.h/cpp     # Game Server 세션
    └── ChatServerChannel.h/cpp     # TCP 클라이언트 (→ Chat :8083)
```

---

## 클라이언트 연결 흐름

```
Client → TCP :8080 → RoomServer.doAccept()
  → ClientSession 생성 (socket 소유권 이전)
  → doRead() 루프 시작
  → Codec<Envelope>.feed() + tryDecode()
  → processMessage() — Rate Limit 체크 후 RoomManager 디스패치
  → send() — write_queue_ + doWrite() 체인
```

### Envelope Dispatch

| payload_case | 핸들러 |
|-------------|--------|
| kCreateRoom | `RoomManager::handleCreateRoom` |
| kJoinRoom | `RoomManager::handleJoinRoom` |
| kLeaveRoom | `RoomManager::handleLeaveRoom` |
| kToggleReady | `RoomManager::handleToggleReady` |
| kStartGame | `RoomManager::handleStartGame` |
| kRoomListRequest | `RoomManager::handleRoomListRequest` |
| kHeartbeat | 타이머 리셋 (doRead에서 처리) |

### Heartbeat

- 메시지 수신 시마다 타이머 리셋 (기본 30초)
- 타임아웃 시 close() → handleDisconnect()

---

## Room 상태 머신

```
ROOM_WAITING
  ├── addPlayer() — 가능 (최대 8명)
  ├── removePlayer() — 가능
  ├── toggleReady() — 비호스트만
  └── canStart() → true 시 → ROOM_IN_GAME

ROOM_IN_GAME
  ├── 새 플레이어 참가 불가
  ├── SlotReleased로 플레이어 제거
  └── 전원 퇴장 시 방 삭제
```

### 호스트 퇴장

호스트가 나가면 방 전체 삭제 → 나머지 멤버에게 `ROOM_CLOSED` RejectResponse 전송.

---

## 게임 시작 흐름

```
Host: StartGameRequest
  │
  ├── canStart() 검증 (비호스트 전원 Ready)
  │
  ├── Room 상태 → ROOM_IN_GAME
  │
  ├── session_id 생성 (UUID)
  │
  ├── Redis: registerGameSession(session_id)
  │
  ├── 각 플레이어에게:
  │   ├── auth_token = createToken(player_id, session_id)
  │   └── GameStart { session_id, auth_token, host, port } 전송
  │
  └── ChatServerChannel: sendSessionCreated(session_id, players)
```

---

## 내부 채널

### GameServerChannel (:8081)

Game Server가 접속하여 토큰 검증, 슬롯 해제, 하트비트를 처리.

| 메시지 | 방향 | 처리 |
|--------|------|------|
| TokenValidateRequest | Game → Room | SessionStore.validateToken → 응답 |
| SlotReleased | Game → Room | RoomManager.handleSlotReleased → 방에서 플레이어 제거 |
| GameServerHeartbeat | Game → Room | SessionStore.updateGameServerHeartbeat (TTL 갱신) |

- localhost 전용 (127.0.0.1)
- Game Server 연결 끊김 시: 진행 중인 모든 게임 방 강제 종료

### ChatServerChannel (→ Chat :8083)

Room Server가 Chat Server에 TCP 클라이언트로 접속. 단방향 메시징.

| 메시지 | 시점 |
|--------|------|
| SessionCreated | 게임 시작 시 (session_id + 플레이어 목록) |
| SessionEnded | 방 전원 퇴장 시 (session_id) |

- 5초 간격 자동 재연결
- 미연결 시 메시지 드롭 (fail-open)

---

## RoomManager 핵심 자료구조

```cpp
unordered_map<room_id, shared_ptr<Room>> rooms_;
unordered_map<player_id, room_id> player_to_room_;
unordered_map<player_id, weak_ptr<ClientSession>> sessions_;
```

- 단일 io_context 스레드에서 실행 (mutex 불필요)
- sessions_에 weak_ptr 사용 (순환 참조 방지)

---

## 초기화 순서 (main.cpp)

1. Logger::init("room")
2. Config 로드
3. RedisClient 연결
4. SessionStore 생성
5. RateLimiter 생성
6. io_context 생성
7. ChatServerChannel 생성 (→ :8083 비동기 연결)
8. RoomManager 생성
9. RoomServer 생성 (:8080)
10. GameServerChannel 생성 (:8081)
11. SIGINT/SIGTERM 핸들러 등록
12. 전체 start() → io_context.run()
