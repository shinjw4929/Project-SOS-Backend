# Phase 1: ClickHouse 스키마 + C++ 로깅 코드

## 목표

채팅 메시지 전용 ClickHouse 테이블을 정의하고, Chat Server에서 모든 채널의 메시지 내용을 spdlog로 출력한다.

## 전제 조건

없음 (첫 번째 Phase)

## 작업 목록

### 병렬 작업

- [ ] **Task A: ClickHouse 테이블 생성** — `infra/clickhouse/init.sql` 끝에 `chat_messages` 테이블 추가
- [ ] **Task B: C++ 로깅 코드 추가** — `src/chat/channel/ChannelManager.cpp`의 `handleChatSend()`에 메시지 로깅

### Task A 상세: ClickHouse 스키마

`infra/clickhouse/init.sql` 파일 끝에 추가:

```sql
-- 채팅 메시지 영구 저장 테이블
CREATE TABLE project_sos.chat_messages (
    timestamp   DateTime64(3, 'UTC'),
    session_id  String DEFAULT '',
    channel     LowCardinality(String),
    sender_id   String,
    sender_name String,
    target_id   String DEFAULT '',
    content     String
) ENGINE = MergeTree()
PARTITION BY toYYYYMMDD(timestamp)
ORDER BY (session_id, timestamp)
TTL toDate(timestamp) + INTERVAL 90 DAY DELETE;
```

설계 근거:
- `session_id`: LOBBY 메시지는 빈값, ALL/WHISPER는 세션 ID
- `target_id`: WHISPER만 사용, 나머지는 빈값
- 파티셔닝: 날짜별 (서비스 단일이므로 service 파티션 불필요)
- ORDER BY: 세션별 + 시간순 (신고 조회, 세션 내 채팅 이력 조회가 주 패턴)
- TTL: `service_events`와 동일하게 90일

### Task B 상세: C++ 로깅

`src/chat/channel/ChannelManager.cpp`의 `handleChatSend()` 함수에서, ChatReceive 생성 직후/채널 라우팅 직전에 spdlog 호출 추가.

현재 코드 위치: 142~150행 (ChatReceive 생성) → 152행 (switch 시작) 사이

추가할 코드:

**1. content sanitization** — `handleChatSend()` 내부, switch 직전에 개행/쉼표 치환:

```cpp
// 로깅용 필드 sanitize
auto strip_newlines = [](std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    std::replace(s.begin(), s.end(), '\r', ' ');
    return s;
};
// sender_name은 중간 필드이므로 쉼표도 치환 (Vector 정규식 [^,]+ 보호)
// content는 마지막 필드이므로 쉼표 유지 (원본 보존, .* 정규식으로 안전)
std::string safe_name = strip_newlines(state.player_name);
std::replace(safe_name.begin(), safe_name.end(), ',', ' ');
std::string safe_content = strip_newlines(content);
```

- 개행(`\n`/`\r`): 모든 클라이언트 필드에서 치환 (spdlog 줄 단위 출력 보호)
- 쉼표(`,`): `sender_name`만 치환 (중간 필드, Vector `[^,]+` 파싱 보호). `content`는 마지막 필드이므로 `.*`로 캡처되어 쉼표가 있어도 안전하며, 원본 메시지를 보존

**2. 로깅 위치** — switch 직전이 아니라 **각 채널의 성공 경로 직후**에 배치:

```cpp
// CHANNEL_LOBBY 성공 경로 (broadcastToLobby() 직후)
spdlog::info("[Chat:Message] channel=LOBBY, sender_id={}, sender_name={}, session_id={}, target_id=, content={}",
    player_id, safe_name, state.session_id, safe_content);

// CHANNEL_ALL 성공 경로 (broadcastToSession() + saveToHistory() 직후)
spdlog::info("[Chat:Message] channel=ALL, sender_id={}, sender_name={}, session_id={}, target_id=, content={}",
    player_id, safe_name, state.session_id, safe_content);

// CHANNEL_WHISPER 성공 경로 (sendTo(target) + sendTo(sender) 모두 완료 후 1회만 로깅)
spdlog::info("[Chat:Message] channel=WHISPER, sender_id={}, sender_name={}, session_id={}, target_id={}, content={}",
    player_id, safe_name, state.session_id, message.whisper_target(), safe_content);
```

switch 직전에 로깅하면 `CHANNEL_NOT_JOINED`/`PLAYER_NOT_FOUND`로 거부된 메시지도 저장되므로, 실제 전송된 메시지만 기록하기 위해 각 성공 경로에 배치한다.

출력 예시:
```
2026-04-08 10:00:01.100 | INFO    | [Chat:Message] channel=LOBBY, sender_id=player1, sender_name=John, session_id=, target_id=, content=hello everyone
2026-04-08 10:00:02.200 | INFO    | [Chat:Message] channel=ALL, sender_id=player1, sender_name=John, session_id=sess-abc, target_id=, content=watch out
2026-04-08 10:00:03.300 | INFO    | [Chat:Message] channel=WHISPER, sender_id=player1, sender_name=John, session_id=sess-abc, target_id=player2, content=hey
```

주의사항:
- `content`는 반드시 마지막 필드로 배치 (내용에 쉼표 포함 가능, `.*` 정규식으로 캡처)
- 각 경로에서 채널명을 리터럴로 직접 사용 (별도 헬퍼 함수 불필요)
- 개행/제어 문자는 sanitize 후 로깅 (Vector 파싱 보호)
- Rate Limit/검증 통과 + 채널 멤버십 확인 통과 후, 실제 전송되는 메시지만 로깅

## 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `infra/clickhouse/init.sql` | `chat_messages` 테이블 CREATE TABLE 추가 |
| `src/chat/channel/ChannelManager.cpp` | `handleChatSend()` 각 성공 경로에 spdlog::info + content sanitization |

## 검증

- [ ] C++ 빌드 성공 (`cmake --build build`)
- [ ] Chat Server 실행 시 메시지 전송하면 stdout에 `[Chat:Message]` 로그 출력 확인
- [ ] ClickHouse 컨테이너 재생성 시 `chat_messages` 테이블 생성 확인 (`SHOW TABLES FROM project_sos`)
