# Room List Push Broadcast — Orchestration

## 문제 정의

현재 방 목록 갱신은 클라이언트가 `RoomListRequest`를 명시적으로 보내야만 서버가 `RoomListResponse`로 응답하는 **Pull 방식**으로만 동작한다. 클라이언트는 로비 진입 시 0.5초 후 1회 자동 요청하고, 이후에는 유저가 새로고침 버튼을 눌러야 재요청한다. 이로 인해 다른 유저가 방을 생성/변경해도 즉시 반영되지 않는다.

## AS-IS

### 클라이언트 → 서버 흐름

1. 클라이언트가 `RoomListRequest(page, page_size)` 전송
2. `ClientSession::processMessage` → `RoomManager::handleRoomListRequest`
3. ROOM_WAITING 상태 방만 필터링, 페이지네이션 적용
4. `RoomListResponse` 빌드 후 요청한 세션에만 응답

### 로비 클라이언트 추적

- **없음**: RoomManager의 `sessions_` 맵은 방에 입장한 플레이어만 추적 (player_id → weak_ptr)
- 방에 참가하지 않은 로비 클라이언트는 어디에서도 추적되지 않음
- `RoomListRequest` 핸들러는 세션을 직접 받아 1:1 응답만 수행

### 방 변동 이벤트

현재 방 목록에 영향을 주는 이벤트 발생 시 로비 클라이언트에 알리는 메커니즘이 없음:

| 이벤트 | 발생 위치 | 방 목록 영향 |
|--------|----------|-------------|
| 방 생성 | `handleCreateRoom` | 새 방 추가 |
| 플레이어 참가 | `handleJoinRoom` | 인원 수 변경 |
| 플레이어 퇴장 | `handleLeaveRoom` | 인원 수 변경 또는 방 삭제 |
| 게임 시작 | `handleStartGame` | WAITING → IN_GAME (목록에서 제거) |
| 슬롯 해제 후 빈 방 | `handleSlotReleased` | 방 삭제 |
| 게임 서버 단절 | `handleGameServerDisconnect` | IN_GAME 방 일괄 삭제 |

## TO-BE

### 로비 세션 추적

- RoomManager에 `lobby_sessions_` 자료구조 추가 (raw pointer → weak_ptr 맵)
- 클라이언트 상태 전이에 따라 추가/제거:
  - **연결** → 로비 등록
  - **방 생성/참가** → 로비에서 제거
  - **방 퇴장/방 폐쇄** → 로비로 복귀
  - **연결 해제** → 로비에서 제거

### 디바운스 Push 브로드캐스트

- 방 변동 이벤트 발생 시 `notifyRoomListChanged()` 호출
- 300ms 디바운스 타이머로 연속 이벤트를 하나의 브로드캐스트로 병합
- `broadcastRoomListToLobby()`가 첫 페이지(page=0, size=20)를 모든 로비 세션에 전송

### 프로토콜 변경

- **없음**: 기존 `RoomListResponse` 메시지를 그대로 사용
- 클라이언트는 `PayloadCase == RoomListResponse`이면 요청 여부와 무관하게 처리 가능

## Phase 체크리스트

| Phase | 파일 | 목표 | 상태 |
|-------|------|------|------|
| 1 | phase-1-lobby-session-tracking.md | 로비 세션 추적 인프라 구축 | 완료 |
| 2 | phase-2-push-broadcast.md | 디바운스 방 목록 Push 브로드캐스트 구현 | 완료 |

## Phase 의존성

```
Phase 1 → Phase 2 (순차)
```

Phase 2는 Phase 1의 로비 세션 추적에 의존한다.

## 검증 방법

1. 빌드 성공 (`cmake --build build`)
2. 기존 유닛 테스트 통과 (`ctest --output-on-failure`)
3. 수동 테스트:
   - 클라이언트 A가 로비 진입 → 클라이언트 B가 방 생성 → A가 새로고침 없이 방 목록 갱신 확인
   - 방 참가/퇴장/삭제/게임 시작 시 로비 클라이언트에 자동 갱신 확인
   - 연속 이벤트 발생 시 디바운스로 브로드캐스트 병합 확인

## 롤백 계획

- **Phase 1**: 로비 추적 코드 제거, RoomManager 생성자 원복. 기존 동작에 영향 없음.
- **Phase 2**: notifyRoomListChanged 호출부와 타이머/브로드캐스트 로직 제거. Phase 1 상태로 복귀.
