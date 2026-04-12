# Grafana 대시보드 쿼리 수정 및 패널 정리

> Chat Metrics / Room Metrics 대시보드의 쿼리 버그 수정, 패널 이름 변경, 배치 순서 조정.

---

## 개요

Grafana 대시보드에서 데이터가 표시되지 않는 패널들을 조사하여, ClickHouse 쿼리의 LIKE 패턴이 실제 C++ 서버 로그 메시지와 불일치하는 문제를 수정했다. 추가로 패널 이름을 실제 내용에 맞게 변경하고, Chat Metrics의 패널 배치 순서를 조정했다.

---

## 변경 1: Authentication Events 쿼리 수정

### 문제

Chat Metrics의 Authentication Events 패널이 데이터를 표시하지 않았다. 쿼리의 LIKE 패턴이 실제 로그 메시지와 불일치.

```sql
-- 변경 전 (매칭 실패)
message LIKE '%lobby auth%'
message LIKE '%session auth%'

-- 변경 후 (실제 로그 패턴에 맞춤)
message LIKE 'Player authenticated (lobby)%'
message LIKE 'Player authenticated (session)%'
```

### 근거

`src/chat/channel/ChannelManager.cpp` line 77-78, 107-108에서 실제 출력하는 로그:

```
[Chat] Player authenticated (lobby), player_id=..., player_name=...
[Chat] Player authenticated (session), player_id=..., session_id=...
```

---

## 변경 2: Rate Limited / Redis Errors 쿼리 수정

### 문제

ClickHouse LIKE는 대소문자를 구분한다. 쿼리에서 `%rate limit%`만 사용했으나, 실제 로그는 `Rate limit check failed`로 대문자 R.

```sql
-- 변경 전 (대소문자 불일치)
message LIKE '%rate limit%'
message LIKE '%Redis%' OR message LIKE '%redis%'

-- 변경 후
message LIKE '%Rate limit%' OR message LIKE '%rate limit%'
message LIKE '%Redis error%'
```

### 참고

`src/chat/server/ChatSession.cpp` line 152에서 Rate Limit 체크 실패(Redis 에러) 시에만 로그를 남긴다. 실제 Rate Limit 트리거(요청 초과)는 로그가 없으므로, 해당 패널은 Redis 통신 에러만 표시된다.

---

## 변경 3: 패널 이름 변경

두 대시보드의 Internal Channel 테이블 패널 이름이 `Internal Channel Status`였으나, 실제 내용은 상태가 아닌 로그 목록이므로 용도에 맞게 변경.

| 대시보드 | 변경 전 | 변경 후 |
|---------|--------|--------|
| Chat Metrics | Internal Channel Status | Internal Channel Log (Room Server) |
| Room Metrics | Internal Channel Status | Internal Channel Log (Game/Chat Server) |

괄호 안은 해당 Internal Channel의 연결 상대를 표시한다:
- Chat Server의 Internal Channel(`:8083`)은 Room Server가 접속하는 채널
- Room Server의 Internal Channel(`:8081`)은 Game/Chat Server가 접속하는 채널

---

## 변경 4: Chat Metrics 패널 배치 순서 변경

Chat Message Log 테이블을 상단으로 이동하여 접근성을 높였다.

| 위치 (y) | 변경 전 | 변경 후 |
|----------|--------|--------|
| y=24 | Internal Channel Status | Chat Message Log |
| y=32 | Chat Server Errors | Internal Channel Log (Room Server) |
| y=40 | Chat Message Log | Chat Server Errors |

---

## 수정 파일

```
infra/grafana/dashboards/chat-metrics.json   # 쿼리 2건 수정, 패널 이름 변경, 배치 순서 변경
infra/grafana/dashboards/room-metrics.json   # 패널 이름 변경
```

---

## 추가 발견 사항

### Vector 로그 수집 타이밍

Vector의 `docker_logs` 소스는 Vector 기동 이후 발생한 로그만 수집한다. Chat Server가 Vector보다 먼저 기동되면 Chat:Internal 카테고리의 초기 로그(연결 수립 등)가 ClickHouse에 저장되지 않는다. 서버 재시작 시 새 로그가 생성되면서 정상 수집된다.

### Chat Rate Limit 로깅 부재

`ChatSession::checkRateLimit()`은 Redis 통신 에러만 로깅하고, 실제 Rate Limit 초과(요청 거부) 시에는 로그를 남기지 않는다. Rate Limit 모니터링이 필요하면 거부 이벤트 로깅을 추가해야 한다.
