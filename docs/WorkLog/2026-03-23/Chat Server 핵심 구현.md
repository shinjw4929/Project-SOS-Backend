# Chat Server 핵심 구현

> Phase 4 Step 4-1. TCP 서버 + 인증 + 로비 채팅 + 메시지 검증 + 귓속말.

---

## 개요

Chat Server의 핵심 골격을 구현했다. Room Server와 동일한 패턴(Boost.Asio + Protobuf Envelope + 4byte LE 프레이밍)을 따르며, `ChatEnvelope`(chat.proto)를 사용한다. 클라이언트 대면 포트는 :8082.

---

## ChatServer (:8082)

`RoomServer`와 동일한 TCP acceptor 구조.

```
ChatServer(io_context, port, channel_manager, rate_limiter, heartbeat_timeout)
  └── doAccept() → ChatSession 생성
```

---

## ChatSession

`ClientSession`(Room Server)과 동일한 비동기 I/O 패턴.

| 기능 | 구현 |
|------|------|
| 프레이밍 | `Codec<ChatEnvelope>` (4byte LE + Protobuf) |
| 하트비트 | `boost::asio::steady_timer` (기본 90초) |
| Rate Limit | player_id 기반, `chat:rate:` 접두사 (5초당 10회) |
| 인증 상태 | `authenticated_` 플래그, 미인증 시 메시지 거부 |

### 메시지 디스패치

| payload_case | 처리 |
|-------------|------|
| `kAuth` | ChannelManager::handleAuth() |
| `kSend` | 인증 확인 → Rate Limit → ChannelManager::handleChatSend() |
| `kHeartbeat` | 타이머 리셋 (doRead에서 처리) |

Rate Limit 실패 시 `ChatError(RATE_LIMITED)` 전송 (연결은 유지, Room Server와 다름).

---

## ChannelManager

채널 관리와 메시지 라우팅을 담당하는 핵심 클래스.

### 데이터 구조

```cpp
struct PlayerState {
    std::string player_name;
    std::string session_id;  // empty = 로비
    std::weak_ptr<ChatSession> session;
    bool in_lobby = false;
};

std::unordered_map<std::string, PlayerState> players_;      // player_id → 상태
std::unordered_set<std::string> lobby_members_;              // 로비 채널 멤버
std::unordered_map<std::string, SessionChannel> session_channels_;  // (Step 4-2)
```

### ChatAuth 인증 흐름

```
ChatAuth 수신
  ├── player_id/player_name 빈 값 → 실패 응답
  ├── 동일 player_id 기존 연결 존재 → kick (ChatError 전송 + close)
  ├── session_id 빈 문자열 → 로비 모드 (lobby_members_ 추가)
  └── session_id 존재 → 세션 채널 검증 후 참가 (Step 4-2)
```

### 채널별 메시지 처리

| 채널 | 동작 | 검증 |
|------|------|------|
| CHANNEL_LOBBY | lobby_members_ 전체 브로드캐스트 | in_lobby 확인 |
| CHANNEL_ALL | 세션 채널 멤버 브로드캐스트 | session_id 확인 (Step 4-2) |
| CHANNEL_WHISPER | 대상 player_id에 직접 전달 + 발신자 에코 | 대상 존재 확인 |

### 메시지 검증

```
validateMessage(player_id, content, channel)
  ├── 빈 문자열 / 공백만 → MESSAGE_TOO_LONG
  └── content.size() > max_message_length_ → MESSAGE_TOO_LONG
```

### ChatReceive 생성

```cpp
ChatReceive {
    channel,
    sender_id = player_id,
    sender_name = players_[player_id].player_name,
    content,
    timestamp = currentTimestampMs()  // system_clock 밀리초
}
```

---

## 중복 접속 처리

동일 player_id로 새 연결이 들어오면:
1. 기존 연결에 `ChatError(NOT_AUTHENTICATED, "Replaced by new connection")` 전송
2. 기존 연결 close
3. 기존 상태(로비 멤버, 세션 채널 멤버) 정리
4. 새 연결로 상태 교체

이유: 클라이언트 크래시 후 재접속 시 기존 연결이 하트비트 타임아웃까지 남아있을 수 있음.

---

## 파일 구조

```
src/chat/
├── server/
│   ├── ChatServer.h/.cpp       # TCP acceptor (:8082)
│   └── ChatSession.h/.cpp      # 클라이언트 세션
├── channel/
│   └── ChannelManager.h/.cpp   # 채널 관리 + 메시지 라우팅
└── main.cpp                    # Redis + RateLimiter + ChannelManager 배선
```
