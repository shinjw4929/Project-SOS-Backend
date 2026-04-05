# 게임서버 중복세션 방지 -- Orchestration

## 문제 정의

현재 `RoomManager::handleStartGame`은 게임 서버가 이미 다른 세션을 처리 중인지 확인하지 않는다.
단일 게임 서버(Unity Netcode, :7979) 구조에서 두 방이 각각 게임 시작을 요청하면, 두 그룹의 플레이어가 모두 같은 게임 서버에 접속하여 동일한 World에 배치된다.

**재현 시나리오**:
1. 클라이언트 A: 방 생성 -> 게임 시작 -> `GameStart(session_id_A, host:7979)` 수신
2. 클라이언트 B: 방 생성 -> 게임 시작 -> `GameStart(session_id_B, host:7979)` 수신
3. 두 플레이어 모두 7979에 접속 -> 같은 World에서 게임 진행
4. 예상치 못한 엔티티 수로 인해 Unity ECS structural change -> job safety 위반 (`InvalidOperationException`)

## AS-IS

### RoomManager::handleStartGame (`src/room/room/RoomManager.cpp:240`)

```
호스트 확인 -> canStart() 확인 -> ROOM_IN_GAME 전환 -> session_id 생성 -> GameStart 전송
```

- 다른 방이 이미 `ROOM_IN_GAME` 상태인지 **확인하지 않음**
- `game_server_host_`/`game_server_port_`는 RoomManager 전체에서 단일 값 (모든 방이 같은 게임 서버로 라우팅)

### Protobuf RejectReason (`proto/room.proto:104-116`)

- 게임 서버 점유 관련 거부 사유 없음 (UNKNOWN~ROOM_CLOSED, 0~9)

### 자료구조

- `rooms_`: 모든 방 (WAITING + IN_GAME) 저장
- `active_sessions` (Redis SET): 진행 중 세션 ID 저장 (등록만 하고 시작 시 조회하지 않음)

## TO-BE

### 변경 요약

| 항목 | AS-IS | TO-BE |
|------|-------|-------|
| `handleStartGame` 진입 검증 | 호스트/레디만 확인 | + 게임 서버 점유 여부 확인 |
| `RejectReason` | 0~9 (10개) | + `GAME_SERVER_BUSY = 10` 추가 |
| 게임 서버 점유 판단 | 없음 | `rooms_`에서 `ROOM_IN_GAME` 상태 방 존재 여부로 판단 |

### 설계 결정

- **Redis `active_sessions` 대신 로컬 `rooms_` 맵 사용**: 단일 Room Server 인스턴스 구조에서 로컬 상태가 더 빠르고 정확. Redis 조회 지연/장애 리스크 없음.
- **`hasActiveGame()` 헬퍼 메서드 추가**: 재사용성 확보 (향후 방 생성 시 경고 등에도 활용 가능)

## Phase 체크리스트

| Phase | 파일 | 목표 | 상태 |
|-------|------|------|------|
| 1 | phase-1-proto-reject-reason.md | RejectReason에 GAME_SERVER_BUSY 추가 | 완료 |
| 2 | phase-2-start-game-guard.md | handleStartGame에 게임 서버 점유 검증 추가 | 완료 |

## Phase 의존성

```
Phase 1 (Proto) -> Phase 2 (RoomManager)
```

Phase 2는 Phase 1에서 추가된 `GAME_SERVER_BUSY` enum을 사용하므로 순차 실행.

## 검증 방법

1. 빌드 성공 확인
2. 유닛 테스트 통과 확인
3. 수동 테스트: 두 클라이언트가 각각 방 생성 후 게임 시작 -> 두 번째 요청이 `GAME_SERVER_BUSY`로 거부되는지 확인

## 롤백 계획

- Phase 2 롤백: `handleStartGame`에서 검증 로직 제거 (Phase 1은 유지해도 무방)
- Phase 1 롤백: `GAME_SERVER_BUSY = 10` enum 제거 (proto 재생성)
- 전체 롤백: git revert로 커밋 단위 복원
