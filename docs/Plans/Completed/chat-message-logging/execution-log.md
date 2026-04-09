# 채팅 메시지 영구 저장 — Execution Log

| Phase | 시작 | 완료 | 결과 | 비고 |
|-------|------|------|------|------|
| 1 | 2026-04-08 | 2026-04-08 | Pass | ClickHouse 스키마 + C++ 로깅 |
| 2 | 2026-04-08 | 2026-04-08 | Pass | Vector 파이프라인 |
| 3 | 2026-04-08 | 2026-04-08 | Pass | Grafana + 문서 |

## Phase 1: ClickHouse 스키마 + C++ 로깅 코드 - 2026-04-08

### 실행 내역
| 작업 | 결과 | 비고 |
|---|---|---|
| Task A: ClickHouse chat_messages 테이블 추가 | 완료 | init.sql 끝에 CREATE TABLE 추가 |
| Task B: C++ 메시지 로깅 코드 추가 | 완료 | handleChatSend() 각 성공 경로에 spdlog::info + sanitization |
| 빌드 검증 | 완료 | Developer Command Prompt에서 수동 빌드 성공 확인 |

### 변경된 파일
- `infra/clickhouse/init.sql` - chat_messages 테이블 CREATE TABLE 추가 (MergeTree, 날짜 파티션, 90일 TTL)
- `src/chat/channel/ChannelManager.cpp` - `<algorithm>` include 추가, handleChatSend()에 sanitization 로직(개행/쉼표 치환) + 각 채널 성공 경로에 `[Chat:Message]` spdlog::info 로깅

### 발견된 이슈
- 없음

### Phase 1 완료 판정: Pass

## Phase 2: Vector 라우팅 파이프라인 - 2026-04-08

### 실행 내역
| 작업 | 결과 | 비고 |
|---|---|---|
| Task A: route_cpp 트랜스폼 추가 | 완료 | category == "Chat:Message" 기반 분기 |
| Task B: extract_chat_fields 트랜스폼 추가 | 완료 | key-value 정규식 파싱 + 메타데이터 제거 |
| Task C: clickhouse_chat_messages 싱크 추가 | 완료 | batch 500, timeout 5초, 디스크 버퍼 256MB |
| Task D: clean_fields.inputs 수정 | 완료 | parse_cpp → route_cpp._unmatched |
| Task E: Vector 테스트 추가 | 완료 | ALL 정상, WHISPER target_id, LOBBY 쉼표 content 3개 |

### 변경된 파일
- `infra/vector/vector.toml` - route_cpp, extract_chat_fields 트랜스폼 + clickhouse_chat_messages 싱크 추가, clean_fields.inputs 수정
- `infra/vector/vector-tests.toml` - Chat:Message 파싱 테스트 3개 추가

### 발견된 이슈
- 없음

### Phase 2 완료 판정: Pass

## Phase 3: Grafana 대시보드 + 문서 업데이트 - 2026-04-08

### 실행 내역
| 작업 | 결과 | 비고 |
|---|---|---|
| Task A: 패널 5 교체 (Messages by Channel) | 완료 | service_events → chat_messages 테이블 직접 쿼리 |
| Task A: 패널 9 추가 (Chat Message Log) | 완료 | 최근 50개 메시지 테이블 (y=40) |
| Task B: 인프라 구성.md 업데이트 | 완료 | chat_messages 스키마, Vector 파이프라인 분기, 패널 수 |
| Task B: Chat Server.md 업데이트 | 완료 | 메시지 로깅 동작, 영구 저장 경로 |
| Task B: CLAUDE.md 업데이트 | 완료 | Data Flow 분기, chat_messages 테이블, 대시보드 |

### 변경된 파일
- `infra/grafana/dashboards/chat-metrics.json` - 패널 5 쿼리 교체 + 패널 9 추가
- `docs/Systems/인프라 구성.md` - chat_messages 스키마, Vector 파이프라인, Grafana 패널 수
- `docs/Systems/Chat Server.md` - 메시지 라우팅 로깅, 영구 저장 섹션
- `CLAUDE.md` - Data Flow, chat_messages 테이블, Chat Metrics 대시보드

### 발견된 이슈
- 없음

### Phase 3 완료 판정: Pass
