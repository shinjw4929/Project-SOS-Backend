# Phase 2: handleStartGame 게임 서버 점유 검증

## 목표

`RoomManager::handleStartGame`에서 게임 서버가 이미 다른 세션을 처리 중이면 `GAME_SERVER_BUSY`로 거부한다.

## 전제 조건

- Phase 1 완료 (GAME_SERVER_BUSY enum 사용 가능)

## 작업 목록

### 순차 작업

- [x] `src/room/room/RoomManager.cpp`: `#include <algorithm>` 추가 (`std::any_of` 사용)
- [x] `src/room/room/RoomManager.h`: `hasActiveGame()` private 메서드 선언 추가
- [x] `src/room/room/RoomManager.cpp`: `hasActiveGame()` 구현 -- `rooms_`를 순회하여 `ROOM_IN_GAME` 상태 방 존재 여부 반환
- [x] `src/room/room/RoomManager.cpp`: `handleStartGame()` 상단에 `hasActiveGame()` 검증 추가 -- true이면 `GAME_SERVER_BUSY` reject + 로그 출력 후 return
- [x] `docs/Systems/Room Server.md`: 게임 시작 흐름에 게임 서버 점유 검증 단계 추가

## 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `src/room/room/RoomManager.cpp` | `#include <algorithm>` 추가 |
| `src/room/room/RoomManager.h` | `bool hasActiveGame() const;` private 메서드 선언 |
| `src/room/room/RoomManager.cpp` | `hasActiveGame()` 구현 + `handleStartGame()` 진입부에 검증 로직 |
| `docs/Systems/Room Server.md` | 게임 시작 흐름에 점유 검증 단계 반영 |

## 구현 상세

### hasActiveGame()

```cpp
bool RoomManager::hasActiveGame() const {
    return std::any_of(rooms_.begin(), rooms_.end(),
        [](const auto& pair) { return pair.second->state() == sos::room::ROOM_IN_GAME; });
}
```

### handleStartGame() 검증 추가 위치

`isHost()` 검증 직후, `canStart()` 직전에 삽입 (전역 상태를 먼저 확인하여 불필요한 방 내부 검사 생략):

```cpp
if (hasActiveGame()) {
    sos::room::Envelope reject_env;
    auto* reject = reject_env.mutable_reject();
    reject->set_reason(sos::room::RejectResponse::GAME_SERVER_BUSY);
    reject->set_message("Game server is currently in use");
    sendTo(player_id, reject_env);
    spdlog::warn("[Room] Start rejected, game server busy, player={}, room_id={}",
                 player_id, player_it->second);
    return;
}
```

## 검증

- [x] 빌드 성공
- [ ] 기존 테스트 통과
- [ ] 수동 테스트: 방 A 게임 시작 후 방 B 게임 시작 시도 -> GAME_SERVER_BUSY 거부 확인
- [ ] 수동 테스트: 방 A 게임 종료 (SlotReleased로 전원 퇴장) 후 방 B 게임 시작 -> 정상 시작 확인
