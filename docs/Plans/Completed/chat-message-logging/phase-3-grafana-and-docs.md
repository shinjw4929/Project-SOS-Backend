# Phase 3: Grafana 대시보드 + 문서 업데이트

## 목표

Grafana Chat Metrics 대시보드에 채팅 메시지 조회 패널을 추가하고, 관련 시스템 문서를 업데이트한다.

## 전제 조건

- Phase 2 완료 (Vector → ClickHouse `chat_messages` 파이프라인 동작)

## 작업 목록

### 병렬 작업

- [x] **Task A**: Grafana 대시보드 패널 수정/추가
- [x] **Task B**: 시스템 문서 업데이트

### Task A: Grafana 패널 수정/추가

`infra/grafana/dashboards/chat-metrics.json` 변경.

**패널 5 교체: Messages by Channel**

기존 패널 5는 `service_events`에서 `message LIKE 'Message sent%'`를 쿼리하지만, Chat Server에 해당 로그가 없어 미작동 상태. `chat_messages` 테이블 직접 쿼리로 교체한다.

```sql
SELECT
    toStartOfMinute(timestamp) AS time,
    channel,
    count() AS count
FROM project_sos.chat_messages
WHERE timestamp >= $__fromTime AND timestamp <= $__toTime
GROUP BY time, channel
ORDER BY time
```

- 채널별(LOBBY/ALL/WHISPER) 분당 메시지량 추이
- 스택형 타임시리즈
- 기존 패널 위치/크기 유지 (gridPos 변경 불필요)

**패널 9 추가: Chat Message Log (table)**

기존 패널 8 (Chat Server Errors, y=32, h=8) 아래에 배치.

```sql
SELECT
    toString(timestamp) AS time,
    channel,
    sender_name,
    session_id,
    content
FROM project_sos.chat_messages
WHERE timestamp >= $__fromTime AND timestamp <= $__toTime
ORDER BY timestamp DESC
LIMIT 50
```

- 최근 50개 메시지 테이블
- gridPos: `{ "h": 8, "w": 24, "x": 0, "y": 40 }`
- 컬럼: 시각, 채널, 발신자, 세션, 내용

### Task B: 문서 업데이트

**`docs/Systems/인프라 구성.md`**:
- ClickHouse 섹션에 `chat_messages` 테이블 스키마 추가
- Vector 파이프라인 다이어그램에 `route_cpp` 분기 반영
- Grafana 대시보드 목록에 새 패널 반영

**`docs/Systems/Chat Server.md`**:
- 메시지 라우팅 섹션에 `[Chat:Message]` 로깅 동작 추가
- 히스토리 저장 섹션에 "ClickHouse 영구 저장" 경로 추가

**`CLAUDE.md`**:
- Redis 키 구조 테이블: 변경 없음 (Redis 동작은 변경하지 않음)
- ClickHouse Table 섹션이 있다면 `chat_messages` 추가
- Logging Pipeline 섹션의 Data Flow 다이어그램에 분기 반영

## 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `infra/grafana/dashboards/chat-metrics.json` | 패널 5 교체 (Messages by Channel → chat_messages 쿼리) + 패널 9 추가 (Chat Message Log) |
| `docs/Systems/인프라 구성.md` | chat_messages 테이블, Vector 라우팅, Grafana 패널 |
| `docs/Systems/Chat Server.md` | 메시지 로깅 동작 |
| `CLAUDE.md` | Logging Pipeline 섹션 |

## 검증

- [x] Grafana 대시보드 프로비저닝 정상 로드 (JSON 문법 오류 없음)
- [ ] Chat Message Volume 패널에 채널별 데이터 표시
- [ ] Chat Message Log 패널에 최근 메시지 표시
- [x] 문서와 실제 코드/설정이 일치
