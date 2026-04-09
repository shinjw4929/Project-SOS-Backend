# 채팅 메시지 영구 저장 — Orchestration

## 문제 정의

채팅 메시지 내용이 영구 저장되지 않는다. 현재 Redis에 세션별 최근 20개만 2시간 TTL로 임시 보관하며, TTL 만료 후 모든 채팅 기록이 소실된다. 운영 이벤트(접속/인증/에러)만 ClickHouse에 저장되고 있어, 신고 조회나 채팅 분석이 불가능하다.

## AS-IS

### 채팅 메시지 흐름

```
Client → ChatSend { channel, content, whisper_target }
  → ChannelManager::handleChatSend()         [src/chat/channel/ChannelManager.cpp:130]
    → ChatReceive 생성 (channel, sender_id, sender_name, content, timestamp)
    → 채널별 라우팅:
        LOBBY   → broadcastToLobby()
        ALL     → broadcastToSession() + saveToHistory()   ← Redis 임시 저장
        WHISPER → sendTo(target) + sendTo(sender)
```

### Redis 히스토리 (유일한 저장소)

- 키: `chat:history:{session_id}:ALL`
- 형식: Protobuf 바이너리 (ChatReceive)
- 제한: 최근 20개, TTL 2시간
- LOBBY/WHISPER 채널은 저장 자체가 없음

### 운영 로그 파이프라인 (메시지 내용 미포함)

```
Chat Server (spdlog → stdout) → Docker → Vector (parse_cpp) → clean_fields → ClickHouse (service_events)
```

- `parse_cpp` (`infra/vector/vector.toml:59`): 정규식으로 timestamp/level/category/message 추출
- `clean_fields` (`vector.toml:94`): Docker 메타데이터 제거
- `clickhouse` sink (`vector.toml:109`): `service_events` 테이블에 배치 삽입

### ClickHouse 스키마

- 단일 테이블 `service_events` (`infra/clickhouse/init.sql:4`): 전 서비스 통합
- 채팅 메시지 전용 컬럼 없음 (channel, sender_id, content 등)
- Materialized View: error_rate, violation_rate (채팅 무관)

### Grafana

- `chat-metrics.json`: 운영 이벤트 8개 패널 (접속, 인증, 세션, 에러 등)
- 메시지 내용 조회 패널 없음

## TO-BE

### 변경 요약

| 구분 | 변경 대상 | 내용 |
|------|----------|------|
| 추가 | `infra/clickhouse/init.sql` | `chat_messages` 전용 테이블 |
| 추가 | `src/chat/channel/ChannelManager.cpp` | 메시지 내용 spdlog 출력 (`[Chat:Message]` 카테고리) |
| 수정 | `infra/vector/vector.toml` | `route_cpp` 분기 + `extract_chat_fields` + `clickhouse_chat_messages` 싱크 |
| 추가 | `infra/vector/vector-tests.toml` | Chat:Message 파싱 테스트 |
| 수정 | `infra/grafana/dashboards/chat-metrics.json` | 채팅 메시지 조회 패널 추가 |
| 수정 | `docs/Systems/인프라 구성.md` | chat_messages 테이블, Vector 파이프라인 문서 반영 |
| 수정 | `docs/Systems/Chat Server.md` | 메시지 로깅 동작 추가 |

### ClickHouse: `chat_messages` 테이블

```sql
CREATE TABLE project_sos.chat_messages (
    timestamp   DateTime64(3, 'UTC'),
    session_id  String DEFAULT '',
    channel     LowCardinality(String),   -- LOBBY / ALL / WHISPER
    sender_id   String,
    sender_name String,
    target_id   String DEFAULT '',         -- WHISPER 전용
    content     String
) ENGINE = MergeTree()
PARTITION BY toYYYYMMDD(timestamp)
ORDER BY (session_id, timestamp)
TTL toDate(timestamp) + INTERVAL 90 DAY DELETE;
```

### C++ 로그 출력 형식

```
2026-04-08 10:00:01.100 | INFO    | [Chat:Message] channel=LOBBY, sender_id=abc, sender_name=John, session_id=, target_id=, content=hello world
```

- 모든 채널(LOBBY/ALL/WHISPER)의 **실제 전송된** 메시지를 로깅 (각 채널 성공 경로에 배치)
- 채널 이름은 `CHANNEL_` prefix 없이 짧은 형태 (LOBBY/ALL/WHISPER)
- `content`/`sender_name`은 개행 문자를 공백으로 치환 후 출력 (Vector 파싱 보호)
- `content`는 항상 마지막 필드 (내용에 쉼표가 있어도 안전)

### Vector 파이프라인 변경

```
parse_cpp → route_cpp
              ├── chat_message (category == "Chat:Message")
              │   → extract_chat_fields → clickhouse_chat_messages (chat_messages 테이블)
              └── _unmatched
                  → clean_fields (기존) → clickhouse (service_events 테이블)
```

- `clean_fields.inputs` 변경: `["parse_game", "parse_cpp"]` → `["parse_game", "route_cpp._unmatched"]`
- Chat:Message 로그는 `service_events`에 중복 저장되지 않음
- 의도된 부수 효과: 패널 1 "Chat Log Volume"의 전체 로그 볼륨에서 채팅 메시지 로그가 제외됨. 운영 로그 볼륨 지표는 접속/인증/에러 등 운영 이벤트만 반영하는 것이 적절하므로 이를 의도된 동작으로 간주

### Grafana: 채팅 메시지 조회 패널

chat-metrics 대시보드 변경:
1. **패널 5 교체 (Messages by Channel)**: 기존 `service_events` 쿼리 → `chat_messages` 테이블 직접 쿼리 (기존 패널은 `message LIKE 'Message sent%'`를 사용하나 해당 로그가 없어 미작동)
2. **패널 9 추가 (Chat Message Log)**: 최근 메시지 테이블 (timestamp, channel, sender_name, session_id, content)

## Phase 체크리스트

| Phase | 파일 | 목표 | 상태 |
|-------|------|------|------|
| 1 | phase-1-schema-and-logging.md | ClickHouse 테이블 생성 + C++ 로깅 코드 추가 | 완료 |
| 2 | phase-2-vector-pipeline.md | Vector 라우팅/파싱/싱크 구성 + 테스트 | 완료 |
| 3 | phase-3-grafana-and-docs.md | Grafana 패널 추가 + 문서 업데이트 | 완료 |

## Phase 의존성

```
Phase 1 (스키마 + 로깅) → Phase 2 (Vector) → Phase 3 (Grafana + 문서)
```

순차 실행. Phase 1의 로그 포맷이 확정되어야 Phase 2의 파싱 정규식을 작성할 수 있고, Phase 2가 완료되어야 Phase 3에서 chat_messages 테이블 쿼리가 가능하다.

## 검증 방법

1. Docker Compose 전체 재기동 (`docker compose down -v && docker compose up`)
2. Chat Server 접속 → 각 채널(LOBBY/ALL/WHISPER) 메시지 전송
3. ClickHouse에서 `SELECT * FROM project_sos.chat_messages ORDER BY timestamp DESC LIMIT 10` 확인
4. Grafana Chat Metrics 대시보드에서 메시지 패널 확인
5. 기존 `service_events` 테이블에 Chat:Message 로그가 혼입되지 않음 확인

## 롤백 계획

| Phase | 롤백 방법 |
|-------|----------|
| 1 | `init.sql`에서 `chat_messages` 테이블 정의 제거, C++ 로깅 코드 1줄 제거 |
| 2 | `vector.toml`에서 route/extract/sink 제거, clean_fields.inputs 원복 |
| 3 | `chat-metrics.json`에서 추가 패널 제거, 문서 원복 |
