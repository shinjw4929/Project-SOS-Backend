# Room Server Chat 연동

> Phase 4 Step 4-3. Room Server에서 Chat Server로 세션 이벤트 전달.

---

## 개요

Room Server에 TCP 클라이언트(`ChatServerChannel`)를 추가하여 Chat Server의 내부 포트(:8083)에 연결한다. 게임 시작 시 `SessionCreated`, 게임 종료 시 `SessionEnded`를 전송하여 Chat Server의 세션 채널을 제어한다.

---

## ChatServerChannel

### 설계

Room Server의 기존 내부 채널(GameServerChannel)은 TCP **acceptor**(서버)였지만, ChatServerChannel은 TCP **client**다. Room Server가 Chat Server에 능동적으로 연결한다.

| 항목 | 값 |
|------|-----|
| 프로토콜 | `ChatEnvelope` 기반 4byte LE 프레이밍 |
| 연결 대상 | Chat Server :8083 (설정 가능) |
| 재연결 | 5초 간격 자동 재시도 |
| 실패 처리 | 미연결 시 메시지 드롭 (경고 로그) |

### 연결 생명주기

```
start()
  └── doConnect()
        ├── 성공 → connected_ = true, doRead() 시작
        └── 실패 → scheduleReconnect() (5초 후 재시도)

doRead()
  └── 연결 끊김 감지 → close() → scheduleReconnect()

stop()
  └── stopped_ = true, 타이머 취소, 소켓 종료
```

Chat Server가 먼저 기동되지 않아도 Room Server는 정상 동작하며, 백그라운드에서 5초마다 재연결을 시도한다.

### 공개 메서드

```cpp
void sendSessionCreated(const std::string& session_id,
                        const std::vector<std::pair<std::string, std::string>>& players);

void sendSessionEnded(const std::string& session_id);
```

미연결 상태에서 호출하면 경고 로그 후 메시지를 드롭한다. 채팅은 게임 기능에 필수가 아니므로 이 동작이 허용된다.

---

## RoomManager 변경

### 생성자 확장

```cpp
// 변경 전
RoomManager(max_rooms, max_players_per_room, session_store, game_server_host, game_server_port);

// 변경 후
RoomManager(max_rooms, max_players_per_room, session_store, game_server_host, game_server_port,
            std::shared_ptr<ChatServerChannel> chat_channel = nullptr);
```

`chat_channel`은 기본값 nullptr이므로 기존 테스트 코드에 영향 없음.

### SessionCreated 전송 (handleStartGame)

```
handleStartGame(player_id)
  ... (기존 로직: 상태 전이, 세션 등록, 토큰 발급, GameStart 전송)
  └── chat_channel_->sendSessionCreated(session_id, [{player_id, player_name}, ...])
```

`Room::players()` 접근자를 추가하여 플레이어 ID + 이름 목록을 추출한다.

### SessionEnded 전송

두 지점에서 전송한다:

| 트리거 | 위치 | 조건 |
|--------|------|------|
| 마지막 플레이어 퇴장 | `handleSlotReleased()` | `room->playerCount() == 0` |
| 게임 서버 단절 | `handleGameServerDisconnect()` | 각 IN_GAME 방마다 |

```cpp
if (chat_channel_ && !ended_session_id.empty()) {
    chat_channel_->sendSessionEnded(ended_session_id);
}
```

---

## Room.h 변경

```cpp
// 추가: 플레이어 전체 데이터 접근자 (player_id + player_name 추출용)
const std::vector<PlayerData>& players() const { return players_; }
```

기존 `playerIds()`는 ID만 반환했으므로, SessionCreated에 필요한 player_name을 얻기 위해 추가.

---

## main.cpp 변경

```cpp
// Chat Server 채널 생성
auto chat_channel = std::make_shared<sos::ChatServerChannel>(
    io_context, chat_server_host, chat_server_port);

// RoomManager에 전달
auto room_manager = std::make_shared<sos::RoomManager>(
    max_rooms, max_players_per_room,
    session_store, game_server_host, game_server_port, chat_channel);

// 시작 + 종료
chat_channel->start();
// signal handler: chat_channel->stop();
```

Config에서 `chat_server_host`(기본 127.0.0.1), `chat_server_port`(기본 8083) 읽기.

---

## 전체 메시지 흐름

```
[게임 시작]
  RoomManager::handleStartGame()
    ├── GameStart → 각 클라이언트
    └── ChatServerChannel::sendSessionCreated()
          └── TCP → Chat Server :8083
                └── ChannelManager::handleSessionCreated()
                      └── 세션 채널 생성

[클라이언트 세션 채팅]
  ChatAuth(player_id, session_id) → Chat Server :8082
    └── 세션 채널 참가 + 히스토리 수신
  ChatSend(ALL) → 세션 멤버 브로드캐스트

[게임 종료]
  SlotReleased → RoomManager::handleSlotReleased()
    ├── (모든 플레이어 퇴장 시)
    └── ChatServerChannel::sendSessionEnded()
          └── TCP → Chat Server :8083
                └── ChannelManager::handleSessionEnded()
                      └── 멤버 로비 복귀 + 채널 삭제
```

---

## 파일 구조

```
src/room/internal/
└── ChatServerChannel.h/.cpp   # TCP 클라이언트 (Room → Chat :8083)

src/room/room/
├── Room.h                     # players() 접근자 추가
├── RoomManager.h              # ChatServerChannel 멤버 + 생성자 파라미터
└── RoomManager.cpp            # SessionCreated/SessionEnded 전송 로직

src/room/main.cpp              # ChatServerChannel 생성 + 배선
src/room/CMakeLists.txt        # ChatServerChannel.cpp 추가
```
