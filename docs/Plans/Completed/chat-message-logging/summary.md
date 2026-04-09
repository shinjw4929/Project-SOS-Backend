# 채팅 메시지 영구 저장 — Summary

## 문제 정의

채팅 메시지 내용이 Redis에 임시 저장(최근 20개, 2시간 TTL)만 되어 TTL 만료 후 소실됨. 운영 로그만 ClickHouse에 저장되고 있어 채팅 분석/신고 조회 불가.

## Phase 구성

| Phase | 목표 | 주요 변경 파일 |
|-------|------|---------------|
| 1 | ClickHouse `chat_messages` 테이블 + C++ 로깅 코드 | `init.sql`, `ChannelManager.cpp` |
| 2 | Vector 라우팅 (`Chat:Message` → 별도 테이블) + 테스트 | `vector.toml`, `vector-tests.toml` |
| 3 | Grafana 패널 수정/추가 + 문서 업데이트 | `chat-metrics.json`, docs 3개 |

순차 실행 (Phase 1 → 2 → 3).

## 예상 영향 범위

- **ClickHouse**: 신규 테이블 1개 (기존 `service_events` 영향 없음)
- **Chat Server C++**: `ChannelManager.cpp`에 spdlog 1줄 + 헬퍼 함수 추가
- **Vector**: `route_cpp` 분기 추가, `clean_fields` 입력 변경 (기존 테스트 영향 없음)
- **Grafana**: 패널 5 쿼리 교체 (기존 미작동 → 정상화), 패널 9 신규
- **Redis**: 변경 없음 (기존 임시 히스토리 유지)

## 자동 리뷰

3회차 검토 (plan-create 내장 1회 + review-plan 2회)에서 승인. 누적 수정 반영 항목:

1. 기존 미작동 패널 5를 교체 (중복 패널 방지)
2. `ChatChannel_Name()` 대신 직접 매핑으로 CHANNEL_ prefix 제거
3. content/sender_name 개행 문자 sanitization (Vector 파싱 보호)
4. 로깅 위치를 switch 직전 → 각 채널 성공 경로 직후로 이동 (미전송 메시지 필터)
5. sanitize 람다에 쉼표 치환 추가 (sender_name 쉼표 → Vector 정규식 파싱 실패 방지)
6. ORDER BY `(channel, timestamp)` → `(session_id, timestamp)` (신고/세션 조회 최적화)
7. `CHANNEL_UNKNOWN = 0` case 명시 (컴파일러 경고 방지)
8. Vector 싱크 `timeout_secs` 10 → 5 (기존 싱크와 일관성)
