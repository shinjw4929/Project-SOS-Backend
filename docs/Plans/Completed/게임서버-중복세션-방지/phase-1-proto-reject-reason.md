# Phase 1: Proto RejectReason 추가

## 목표

`RejectReason` enum에 `GAME_SERVER_BUSY = 10`을 추가하여 게임 서버 점유 시 거부 사유를 표현할 수 있도록 한다.

## 전제 조건

없음 (첫 번째 Phase)

## 작업 목록

### 순차 작업

- [x] `proto/room.proto`: `RejectReason` enum에 `GAME_SERVER_BUSY = 10` 추가
- [x] CMake 빌드 실행하여 Protobuf C++ 코드 재생성 확인

## 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `proto/room.proto` | `RejectReason` enum에 `GAME_SERVER_BUSY = 10` 추가 |

## 검증

- [x] 빌드 성공 (Protobuf 코드 재생성 포함)
- [x] 기존 테스트 통과

## Unity 클라이언트 후속 작업 (본 계획 범위 외)

- Unity 프로젝트에서 `Room.cs` (Protobuf 생성 코드) 재생성 필요
- `RoomClient.cs`에서 `GAME_SERVER_BUSY` 거부 사유 처리 (UI 메시지 표시)
