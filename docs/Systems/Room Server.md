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
unordered_map<ClientSession*, weak_ptr<ClientSession>> lobby_sessions_;
```

- 단일 io_context 스레드에서 실행 (mutex 불필요)
- sessions_에 weak_ptr 사용 (순환 참조 방지)
- lobby_sessions_: 방에 참가하지 않은 로비 상태 클라이언트 추적 (Push 브로드캐스트 대상)

---

## 방 목록 Push 브로드캐스트

### 동작 방식

방 목록에 영향을 주는 이벤트 발생 시, 로비 상태의 모든 클라이언트에게 `RoomListResponse`를 자동 Push한다.

- **디바운스**: 300ms 타이머로 연속 이벤트를 하나의 브로드캐스트로 병합
- **전송 내용**: ROOM_WAITING 상태 방의 첫 페이지 (page=0, size=20)
- **프로토콜**: 기존 `RoomListResponse` 재사용 (새 메시지 없음)

### 로비 세션 상태 전이

| 이벤트 | 로비 동작 |
|--------|----------|
| 클라이언트 연결 | lobby 등록 |
| CreateRoom/JoinRoom 성공 | lobby에서 제거 |
| LeaveRoom / 호스트 퇴장 / 게임 서버 단절 | lobby로 복귀 |
| SlotReleased (게임 종료) | lobby로 복귀 (연결 유효 시) |
| 연결 해제 | lobby에서 제거 |

### 브로드캐스트 트리거

| 이벤트 | 발생 위치 |
|--------|----------|
| 방 생성 | handleCreateRoom |
| 플레이어 참가 | handleJoinRoom |
| 플레이어 퇴장 | handleLeaveRoom |
| 게임 시작 | handleStartGame |

---

## 초기화 순서 (main.cpp)

1. Logger::init("room")
2. Config 로드
3. RedisClient 연결
4. SessionStore 생성
5. RateLimiter 생성
6. io_context 생성
7. ChatServerChannel 생성 (→ :8083 비동기 연결)
8. RoomManager 생성 (io_context 전달)
9. RoomServer 생성 (:8080)
10. GameServerChannel 생성 (:8081)
11. SIGINT/SIGTERM 핸들러 등록 (room_manager->stop() + server/channel stop)
12. 전체 start() → io_context.run()
