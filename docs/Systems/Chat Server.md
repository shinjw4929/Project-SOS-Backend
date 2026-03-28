# Chat Server

`src/chat/` — 로비/세션/귓속말 채팅을 담당하는 TCP 서버.

---

## 모듈 구조

```
src/chat/
├── main.cpp                         # 진입점, 초기화 순서
├── server/
│   ├── ChatServer.h/cpp             # TCP acceptor (:8082)
│   └── ChatSession.h/cpp            # 클라이언트 세션
├── channel/
│   └── ChannelManager.h/cpp         # 채널 라우팅, 인증, 히스토리
└── internal/
    ├── InternalChannel.h/cpp        # TCP acceptor (:8083, localhost)
    └── InternalSession.h/cpp        # Room Server 세션
```

---

## 인증 흐름

```
Client → ChatAuth { player_id, session_id, player_name }
  │
  ├── player_id, player_name 빈값 검증
  │
  ├── 중복 접속 처리: 기존 세션에 에러 전송 후 교체
  │
  ├── PlayerState 생성:
  │   ├── session_id 비어있음 → 로비 모드 (lobby_members_ 추가)
  │   └── session_id 있음 → 세션 모드 (session_channels_[].members 추가)
  │       ├── Redis: HSET chat:session:{session_id}
  │       ├── Redis: SETEX chat:player:{player_id}
  │       └── Redis: SETEX chat:name:{player_id}
  │
  ├── ChatAuthResult { success=true } 전송
  │
  └── 세션 모드일 경우: sendHistory() — Redis LRANGE로 최근 메시지 전송
```

---

## 채널 구조

| 채널 | 범위 | 조건 | 히스토리 |
|------|------|------|---------|
| CHANNEL_LOBBY | 로비 전체 | `in_lobby == true` | 없음 |
| CHANNEL_ALL | 같은 세션 전체 | `session_id` 일치 | Redis LIST, 최근 20개, TTL 2시간 |
| CHANNEL_WHISPER | 1:1 | 대상 플레이어 존재 | 없음 |

### 메시지 라우팅

```
ChatSend { channel, content, whisper_target }
  │
  ├── 인증 확인 (미인증 → NOT_AUTHENTICATED 에러)
  ├── Rate Limit 확인 (chat:rate:{player_id})
  ├── 메시지 검증 (빈값, 최대 200바이트)
  │
  ├── CHANNEL_LOBBY → broadcastToLobby()
  ├── CHANNEL_ALL → broadcastToSession() + saveToHistory()
  └── CHANNEL_WHISPER → sendTo(target) + sendTo(sender)  [에코]
```

### 히스토리 저장

```
saveToHistory(session_id, ChatReceive):
  LPUSH chat:history:{session_id}:ALL {직렬화된 메시지}
  LTRIM chat:history:{session_id}:ALL 0 19    # 최근 20개 유지
  EXPIRE chat:history:{session_id}:ALL 7200   # 2시간 TTL
```

---

## 세션 생명주기 (Room Server 연동)

### SessionCreated (게임 시작)

```
Room Server → InternalChannel :8083 → SessionCreated { session_id, players }
  │
  ├── SessionChannel 생성 (expected_players + 빈 members)
  └── Redis: HSET chat:session:{session_id} 에 플레이어 이름 저장
```

### SessionEnded (게임 종료)

```
Room Server → SessionEnded { session_id }
  │
  ├── 세션 멤버 전원 → 로비 모드로 전환
  ├── session_channels_ 에서 삭제
  └── Redis: DEL chat:session:{session_id}, chat:history:{session_id}:ALL
```

---

## ChannelManager 핵심 자료구조

```cpp
struct PlayerState {
    std::string player_name;
    std::string session_id;             // 빈값 = 로비
    std::weak_ptr<ChatSession> session;
    bool in_lobby;
};

struct SessionChannel {
    std::unordered_set<std::string> members;           // 접속 중
    std::unordered_set<std::string> expected_players;  // Room이 알려준 목록
};

unordered_map<player_id, PlayerState> players_;
unordered_set<player_id> lobby_members_;
unordered_map<session_id, SessionChannel> session_channels_;
```

---

## ChatErrorCode

| 코드 | 값 | 발생 조건 |
|------|---|----------|
| NOT_AUTHENTICATED | 1 | 인증 전 ChatSend |
| RATE_LIMITED | 2 | Rate Limit 초과 |
| INVALID_CHANNEL | 3 | 로비 유저가 ALL 전송 등 |
| MESSAGE_TOO_LONG | 4 | 200바이트 초과 또는 빈값 |
| PLAYER_NOT_FOUND | 5 | 귓속말 대상 미존재 |
| CHANNEL_NOT_JOINED | 6 | 세션 미참여 |

---

## 내부 채널 (:8083)

InternalChannel — localhost 전용 TCP acceptor. Room Server 한 대가 접속.

| 메시지 | 처리 |
|--------|------|
| kSessionCreated | ChannelManager::handleSessionCreated |
| kSessionEnded | ChannelManager::handleSessionEnded |

- 하트비트/Rate Limit 없음 (내부 신뢰 연결)
- 수신 전용 (응답 없음)

---

## 초기화 순서 (main.cpp)

1. Logger::init("chat")
2. Config 로드
3. RedisClient 연결
4. RateLimiter 생성 (prefix: "chat:rate:", 10/5초)
5. io_context 생성
6. ChannelManager 생성
7. ChatServer 생성 (:8082)
8. InternalChannel 생성 (:8083)
9. SIGINT/SIGTERM 핸들러 등록
10. 전체 start() → io_context.run()
