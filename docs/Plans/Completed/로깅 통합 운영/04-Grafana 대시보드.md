# Step 5-3: Grafana 대시보드 추가

## Context

기존 대시보드 2개(Service Overview, Game Events)에 Room/Chat 서버 전용 대시보드를 추가한다.

- 데이터소스 UID: `clickhouse-ds`
- 테이블: `project_sos.service_events`
- 프로비저닝: `infra/grafana/dashboards/` (file-based)
- 기존 패턴: `service-overview.json`, `game-events.json` 참조

---

## 수정

### [ ] `infra/grafana/dashboards/room-metrics.json` (~8 패널)

| # | 패널 | 타입 | 쿼리 필터 |
|---|------|------|----------|
| 1 | Room Log Volume | timeseries (stacked) | `service='room'`, level별 분당 집계 |
| 2 | Room Created / Removed | timeseries | `message LIKE 'Room created%'` / `message LIKE 'room removed%'` |
| 3 | Player Join / Leave | timeseries | `Player joined` / `Player left` / `Player disconnected` |
| 4 | Game Started | timeseries | `Game started` 분당 수 |
| 5 | Token / Session Events | timeseries | `Token validated` / `Token invalid` / `Slot released` |
| 6 | Rate Limited Requests | timeseries | rate limit 관련 로그 |
| 7 | Internal Channel Status | table | `category IN ('Room:Internal', 'Room:Chat')` 최근 50개 |
| 8 | Room Server Errors | table | `service='room' AND level IN ('WARNING','ERROR')` 최근 50개 |

### [ ] `infra/grafana/dashboards/chat-metrics.json` (~8 패널)

| # | 패널 | 타입 | 쿼리 필터 |
|---|------|------|----------|
| 1 | Chat Log Volume | timeseries (stacked) | `service='chat'`, level별 분당 집계 |
| 2 | Player Connect / Disconnect | timeseries | `Client connected` / `Player disconnected` |
| 3 | Authentication Events | timeseries | lobby/session 인증 분당 수 |
| 4 | Session Created / Ended | timeseries | `Session created` / `Session ended` 분당 수 |
| 5 | Messages by Channel | timeseries | LOBBY / ALL / WHISPER 채널별 메시지 수 (**주의**: 현재 Chat 서버가 메시지 송수신 이벤트를 spdlog로 기록하지 않으므로, 이 패널 구현 전에 `ChannelManager`에 메시지 전송 로그 추가 필요) |
| 6 | Rate Limited / Errors | timeseries | rate limit + redis error 분당 수 |
| 7 | Internal Channel Status | table | `category='Chat:Internal'` 최근 50개 |
| 8 | Chat Server Errors | table | `service='chat' AND level IN ('WARNING','ERROR')` 최근 50개 |

---

## 대시보드 완성 후 현황

| 대시보드 | 패널 | 대상 |
|---------|------|------|
| service-overview.json | 9 | 전체 서비스 |
| game-events.json | 7 | Game Server |
| **room-metrics.json** | **8** | **Room Server** |
| **chat-metrics.json** | **8** | **Chat Server** |

---

## 검증

- Grafana (localhost:3000)에서 Room Metrics / Chat Metrics 대시보드 표시 확인
- 각 패널의 ClickHouse 쿼리가 정상 실행되는지 확인
- 시간 범위 변경 시 데이터가 올바르게 갱신되는지 확인
