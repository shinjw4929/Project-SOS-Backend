# Room Server 핵심 구현

> Phase 2 (Step 2-1 ~ 2-3). common 라이브러리(Phase 1) 완료 후 Room Server TCP 서버 + Envelope 디스패치 + 방 관리 로직 구축.

---

## 개요

Boost.Asio 기반 TCP 서버에서 Protobuf Envelope 메시지를 수신하고, 방 생성/참가/퇴장/준비/시작 전체 흐름을 처리하는 Room Server 핵심 구현을 완료했다.

---

## 파일 구조

```
src/room/
├── main.cpp                      # io_context + 시그널 핸들링 + 서버 기동
├── server/
│   ├── RoomServer.h/.cpp         # TCP acceptor (:8080), async_accept 루프
│   └── ClientSession.h/.cpp      # 클라이언트 연결, async_read/write, Envelope 디스패치
└── room/
    ├── Room.h/.cpp               # 개별 방 상태 관리 (PlayerData, 준비/시작 로직)
    └── RoomManager.h/.cpp        # 방 CRUD, 세션 매핑, 브로드캐스트, 방 목록

src/common/util/
├── UuidGenerator.h/.cpp          # UUID v4 생성기 (방 ID, 토큰용)
```

---

## 클래스 설계

### RoomServer

Boost.Asio `tcp::acceptor`로 포트 8080에서 클라이언트 연결을 수락한다.

- `start()`: async_accept 루프 시작
- `stop()`: acceptor 닫기 (graceful shutdown)
- 각 연결마다 `ClientSession` shared_ptr 생성

### ClientSession

개별 TCP 연결을 관리한다. `enable_shared_from_this`로 비동기 핸들러에서 수명 보장.

- `doRead()`: async_read_some → Codec에 feed → tryDecode → processMessage
- `doWrite()`: write_queue 기반 순차 전송 (async_write)
- `processMessage()`: Envelope payload_case()에 따른 oneof 디스패치
- `close()`: closed_ 플래그로 중복 호출 방지, handleDisconnect 트리거

디스패치 매핑:

| payload_case | 핸들러 | player_id 필요 |
|-------------|--------|---------------|
| kCreateRoom | handleCreateRoom | 아니오 (요청에 포함) |
| kJoinRoom | handleJoinRoom | 아니오 (요청에 포함) |
| kLeaveRoom | handleLeaveRoom | 예 |
| kToggleReady | handleToggleReady | 예 |
| kStartGame | handleStartGame | 예 |
| kRoomListRequest | handleRoomListRequest | 아니오 |
| kHeartbeat | (Phase 3) | - |

### Room

개별 방의 상태를 관리한다. 내부적으로 `PlayerData` 구조체 벡터를 사용한다.

- 생성 시 호스트를 첫 번째 플레이어로 자동 추가 (`is_host = true`)
- `canStart()`: 호스트 외 전원 `is_ready == true` (호스트 혼자면 vacuously true)
- `toggleReady()`: 호스트는 토글 불가
- `toRoomInfo()` / `toRoomSummary()`: Protobuf 변환

### RoomManager

방 생성/참가/퇴장/준비/시작 비즈니스 로직과 세션 관리를 담당한다.

3개 맵으로 상태 관리:

| 맵 | 키 → 값 | 용도 |
|----|---------|------|
| `rooms_` | room_id → shared_ptr\<Room\> | 방 인스턴스 |
| `player_to_room_` | player_id → room_id | 플레이어-방 매핑 |
| `sessions_` | player_id → weak_ptr\<ClientSession\> | 메시지 전송용 |

주요 검증 흐름 (CreateRoom/JoinRoom):
1. 다른 세션에서 동일 player_id 사용 중 → DUPLICATE_PLAYER
2. 세션에 이미 다른 player_id 설정 → INVALID_REQUEST
3. 이미 방에 참가 중 → ALREADY_IN_ROOM
4. (JoinRoom) 방 미존재 / 게임 중 / 만석 → ROOM_NOT_FOUND / INVALID_REQUEST / ROOM_FULL

호스트 이탈 처리:
- 호스트가 LeaveRoom 또는 연결 끊김 → 방 즉시 삭제
- 남은 플레이어에게 RejectResponse(ROOM_CLOSED) 전송
- 남은 플레이어의 player_to_room_ 매핑 제거

---

## Protobuf 변경

`proto/room.proto`의 `RejectReason` enum에 `ROOM_CLOSED = 9` 추가.
계획 문서에 명시된 거부 사유였으나 proto에 누락되어 있었다.

---

## 의존성 추가

vcpkg.json에 `boost-asio` 추가.

| 패키지 | 버전 | CMake 타겟 |
|--------|------|-----------|
| boost-asio | 1.90.0 | `Boost::system` |

room-server CMakeLists에서 `find_package(Boost REQUIRED COMPONENTS system)` 사용.
Windows에서 `_WIN32_WINNT=0x0A00` 정의 필요.

---

## common 라이브러리 변경

### UuidGenerator 추가

UUID v4를 생성하는 유틸리티 함수. Room ID와 인증 토큰 생성에 사용한다.
Room Server 전용이 아닌 common에 배치하여 Chat Server에서도 재사용 가능.

- `thread_local std::mt19937`으로 스레드 안전
- RFC 4122 UUID v4 형식: version=4, variant=1

### Codec.h 수정

`encode()` 메서드에서 `std::string`을 사용하나 `<string>` 헤더가 누락되어 있어 추가.
Phase 1에서 MSVC 환경 없이 빌드하여 발견되지 않았던 버그.

---

## 빌드 체계

### CMake 타겟 변경

```
room-server (EXECUTABLE)
├── main.cpp
├── server/RoomServer.cpp
├── server/ClientSession.cpp
├── room/Room.cpp
├── room/RoomManager.cpp
├── sos_common (STATIC)
└── Boost::system
```

### 빌드 환경

MSVC 빌드 시 Developer Command Prompt 또는 VS DevShell 환경이 필요하다.
`INCLUDE`, `LIB` 환경변수가 설정되지 않으면 표준 헤더를 찾지 못한다.

CLion에서는 Visual Studio 툴체인 설정으로 자동 처리된다.

---

## 설계 결정

### 싱글 스레드 모델

io_context.run()을 단일 스레드에서 호출한다.
모든 비동기 핸들러가 같은 스레드에서 실행되므로 RoomManager에 mutex가 불필요하다.
Phase 5 이후 성능이 필요하면 strand 기반 멀티스레드로 전환 가능.

### weak_ptr 세션 관리

RoomManager가 ClientSession을 `weak_ptr`로 보유한다.
비동기 핸들러의 `shared_from_this()`가 세션 수명을 관리하고, RoomManager는 메시지 전송 시에만 lock()으로 유효성을 확인한다.

### GameStart 토큰 (임시)

Phase 2에서는 UUID만 생성하여 GameStart에 포함한다.
Phase 3에서 Redis에 토큰을 저장하고 TTL 기반 검증을 추가할 예정.

---

## 메시지 흐름 요약

```
[Client A] ──CreateRoomRequest──► [RoomServer :8080]
                                       │ Room 생성, A = Host
                                       └── CreateRoomResponse → A

[Client B] ──JoinRoomRequest──► [RoomServer]
                                       ├── JoinRoomResponse → B
                                       └── RoomUpdate → A

[Client B] ──ToggleReadyRequest──► [RoomServer]
                                       └── RoomUpdate → A, B

[Client A] ──StartGameRequest──► [RoomServer]
                                       │ canStart() 확인
                                       │ Room → IN_GAME
                                       ├── GameStart(token_A) → A
                                       └── GameStart(token_B) → B
```
