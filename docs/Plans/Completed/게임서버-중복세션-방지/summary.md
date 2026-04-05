# 게임서버 중복세션 방지 -- Summary

## 문제 정의

`RoomManager::handleStartGame`에서 게임 서버가 이미 다른 세션을 처리 중인지 확인하지 않아, 두 방이 동시에 게임을 시작하면 모든 플레이어가 같은 게임 서버 World에 배치됨.

## Phase 구성

| Phase | 목표 | 변경 파일 |
|-------|------|----------|
| 1 | `GAME_SERVER_BUSY` RejectReason 추가 | `proto/room.proto` |
| 2 | `handleStartGame`에 점유 검증 + 문서 업데이트 | `RoomManager.h/cpp`, `docs/Systems/Room Server.md` |

## 예상 영향 범위

- **Room Server**: `RoomManager` 클래스 (handleStartGame 진입 검증 추가)
- **Protobuf**: `RejectReason` enum 확장 (하위 호환, 기존 값 변경 없음)
- **Unity 클라이언트**: Proto 재생성 + `GAME_SERVER_BUSY` 처리 필요 (본 계획 범위 외)

## 자동 리뷰

1회차에 승인. `#include <algorithm>` 누락, 로그 패턴, 문서 업데이트 3건 직접 반영 완료.
