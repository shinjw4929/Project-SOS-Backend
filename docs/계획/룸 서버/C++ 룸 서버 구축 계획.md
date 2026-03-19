# C++ 룸 서버 구축 계획

> **목적**: Unity 게임 서버(:7979) 앞단에 위치하여 클라이언트의 방 생성/참가/준비/시작을 관리하는 서버 구축.
> 방 생성 → 방 목록 조회 → 참가 → 전원 준비 → 호스트 게임 시작 → 토큰 기반 입장 검증.
> PvE 협동 Wave Defense RTS, 1~8인 가변 인원.
> **프로토콜 정의**: [`room.proto`](../../../proto/room.proto) (Single Source of Truth, package `sos.room`)

---

## 기술 스택

| 항목 | 선택 | 버전 | 용도 |
|------|------|------|------|
| 언어 | C++20 | MSVC 19.44 | coroutine, concepts |
| 빌드 | CMake + Ninja | 4.2 | 빌드 시스템 |
| 패키지 | vcpkg | latest | 라이브러리 관리 |
| 비동기 I/O | Boost.Asio | 1.84+ | TCP 서버, 비동기 소켓 |
| Redis 클라이언트 | hiredis + redis-plus-plus | 1.3.0 / 1.3.15 | 세션 TTL, Rate Limit |
| 직렬화 | Protobuf | 6.33.4 (protoc 33.4.0) | 스키마 기반 메시지, C++/C# 코드젠 |
| 로깅 | spdlog | 1.17.0 | 구조화된 로그 출력 |
| 테스트 | Catch2 | 3.5+ | 단위/통합 테스트 |
| 외부 서비스 | Redis | 7.0+ | 인메모리 데이터 스토어 |

### 직렬화: Protobuf 선택 근거
- `.proto` 파일 하나로 C++(룸 서버) / C#(Unity 클라이언트, 게임 서버) 양쪽 코드젠
- 게임 업계 표준 직렬화 포맷 (gRPC 기반 경험과 연결)
- 강타입 스키마로 프로토콜 버전 관리 용이
- Envelope oneof 패턴으로 메시지 타입 디스패치 — 별도 타입 헤더 불필요

---

## 준비물

### 1. 개발 환경

| 항목 | 설치 방법 | 현재 버전 |
|------|----------|----------|
| **Visual Studio 2022** | `winget install Microsoft.VisualStudio.2022.Community` | Community, MSVC 19.44 |
| **CMake** | `winget install Kitware.CMake` | 4.2.3 |
| **Ninja** | `winget install Ninja-build.Ninja` | - |
| **vcpkg** | `git clone https://github.com/microsoft/vcpkg C:\vcpkg` → `bootstrap-vcpkg.bat` | `VCPKG_ROOT=C:\vcpkg` |
| **Docker Desktop** | [설치](https://www.docker.com/products/docker-desktop/) | Redis 실행용 |

### 2. Redis 서버

| 방법 | 명령 | 비고 |
|------|------|------|
| **Docker (권장)** | `docker run -d --name redis -p 6379:6379 redis:7-alpine` | 가장 간편, Windows 호환 |
| WSL2 | `sudo apt install redis-server` | WSL 내부에서 실행 |
| Memurai | [다운로드](https://www.memurai.com/) | Windows 네이티브 Redis 호환 |

Docker 방식 권장. RedisInsight로 데이터 확인 가능.

### 3. vcpkg 의존성

현재 `vcpkg.json` (Phase 2 기준):

```json
{
  "name": "project-sos-backend",
  "version": "0.1.0",
  "dependencies": [
    "protobuf",
    "boost-asio",
    "spdlog",
    "nlohmann-json",
    "hiredis",
    "redis-plus-plus"
  ]
}
```

Phase 6에서 추가 예정: `catch2`
```

### 4. Protobuf 스키마

```
Project-SOS-Backend/
└── proto/
    └── room.proto            # 룸 서버 메시지 정의 (package sos.room)
```

C++ 룸 서버와 Unity 프로젝트 양쪽에서 참조.
Unity 측은 `Google.Protobuf` NuGet 패키지 + `protoc`로 C# 코드젠.

---

## Protobuf 메시지 구조 (room.proto)

### Client → Room Server (:8080)

| 메시지 | 필드 | 용도 |
|--------|------|------|
| `CreateRoomRequest` | player_id, player_name, room_name, max_players | 방 생성 |
| `JoinRoomRequest` | player_id, player_name, room_id | 방 참가 |
| `LeaveRoomRequest` | - | 방 퇴장 |
| `ToggleReadyRequest` | - | 준비 상태 토글 |
| `StartGameRequest` | - | 게임 시작 (호스트 전용) |
| `RoomListRequest` | - | 방 목록 조회 |
| `RoomHeartbeat` | - | 클라이언트 하트비트 |

### Room Server → Client

| 메시지 | 필드 | 용도 |
|--------|------|------|
| `CreateRoomResponse` | success, room (RoomInfo), reason | 방 생성 결과 |
| `JoinRoomResponse` | success, room (RoomInfo), reason | 방 참가 결과 |
| `RoomListResponse` | rooms[] (RoomInfo[]) | 방 목록 |
| `RoomUpdate` | room (RoomInfo) | 방 상태 변경 브로드캐스트 |
| `GameStart` | session_id, auth_token, game_server_host, game_server_port | 게임 시작 정보 |
| `RejectResponse` | reason, message | 요청 거부 |

### Internal (Game Server ↔ Room Server :8081)

| 메시지 | 방향 | 용도 |
|--------|------|------|
| `TokenValidateRequest` | Game → Room | 토큰 검증 요청 |
| `TokenValidateResponse` | Room → Game | 토큰 검증 결과 |
| `SlotReleased` | Game → Room | 플레이어 슬롯 반환 |
| `GameServerHeartbeat` | Game → Room | 게임 서버 생존 확인 |

### 공유 메시지 타입

| 메시지 | 필드 | 용도 |
|--------|------|------|
| `RoomState` | enum: WAITING, IN_GAME | 방 상태 |
| `PlayerInfo` | player_id, player_name, is_ready, is_host | 플레이어 정보 |
| `RoomInfo` | room_id, room_name, host_id, max_players, state, players[] | 방 정보 |

### Envelope

```protobuf
message Envelope {
  oneof payload {
    // Client → Server
    CreateRoomRequest    create_room_request    = 1;
    JoinRoomRequest      join_room_request      = 2;
    LeaveRoomRequest     leave_room_request     = 3;
    ToggleReadyRequest   toggle_ready_request   = 4;
    StartGameRequest     start_game_request     = 5;
    RoomListRequest      room_list_request      = 6;
    RoomHeartbeat        room_heartbeat         = 7;

    // Server → Client
    CreateRoomResponse   create_room_response   = 10;
    JoinRoomResponse     join_room_response     = 11;
    RoomListResponse     room_list_response     = 12;
    RoomUpdate           room_update            = 13;
    GameStart            game_start             = 14;
    RejectResponse       reject_response        = 15;

    // Internal
    TokenValidateRequest  token_validate_request  = 20;
    TokenValidateResponse token_validate_response = 21;
    SlotReleased          slot_released           = 22;
    GameServerHeartbeat   game_server_heartbeat   = 23;
  }
}
```

---

## 프로젝트 구조

```
src/
├── common/          # STATIC 라이브러리 (Room + Chat 공유)
│   ├── protocol/    # Codec<T> 템플릿 (4byte LE 프레이밍 + Protobuf)
│   ├── redis/       # RedisClient (redis-plus-plus 래퍼), SessionStore
│   ├── ratelimit/   # RateLimiter (Redis INCR + EXPIRE)
│   └── util/        # Config (nlohmann/json), Logger (spdlog)
│
├── room/            # Room Server 실행 파일
│   ├── main.cpp
│   ├── server/
│   │   ├── RoomServer.h/.cpp         # Asio TCP 서버 (accept :8080)
│   │   └── ClientSession.h/.cpp      # 개별 클라이언트 연결 관리
│   ├── room/
│   │   ├── RoomManager.h/.cpp        # 방 생성/삭제/조회, 방 목록 관리
│   │   ├── Room.h/.cpp               # 개별 방 상태 (참가/퇴장/준비/시작)
│   │   └── TokenGenerator.h/.cpp     # 인증 토큰 생성 (UUID v4)
│   └── internal/
│       ├── GameServerChannel.h/.cpp  # 게임 서버 내부 TCP 채널 (:8081)
│       └── ChatServerChannel.h/.cpp  # 채팅 서버 연동
│
└── chat/            # Chat Server 실행 파일
    ├── server/
    ├── channel/
    └── internal/

tests/
├── unit/                              # 유닛 테스트 (외부 의존 없음)
│   ├── test_room.cpp                  # Room 참가/퇴장/준비/시작/상태 전환
│   ├── test_room_manager.cpp          # RoomManager 방 생성/삭제/조회/목록
│   ├── test_token_generator.cpp       # UUID 생성, 유일성, 포맷
│   ├── test_codec.cpp                 # Envelope 직렬화/역직렬화 라운드트립
│   ├── test_config.cpp                # 설정 파일 파싱, 기본값, 누락 필드
│   └── test_ratelimit_logic.cpp       # Rate Limit 카운터 로직 (Redis mock)
│
├── feature/                           # 피처 테스트 (실제 TCP/Redis 연동)
│   ├── test_client_connect.cpp        # TCP 접속/해제/하트비트 타임아웃
│   ├── test_room_flow.cpp             # 방 생성→참가→준비→시작 E2E
│   ├── test_room_list.cpp             # 방 목록 조회, 필터링, 실시간 갱신
│   ├── test_token_validate.cpp        # 토큰 발급→Redis 저장→검증→만료→일회용
│   ├── test_ratelimit_redis.cpp       # Redis 기반 Rate Limit 실동작
│   ├── test_internal_channel.cpp      # 내부 채널: 토큰 검증 + SlotReleased + 하트비트
│   └── test_edge_cases.cpp            # 호스트 이탈, 동시 참가, 중복 요청, 비정상 종료
│
└── CMakeLists.txt                     # 테스트 빌드 설정 (unit/feature 분리)

loadtest/
├── load_client.py                     # Python asyncio 부하 테스트 클라이언트
└── requirements.txt                   # protobuf, asyncio 의존성

config/
└── server_config.json                 # 서버 설정
```

---

## 클라이언트 세션 상태 머신

```
[Connected] → CreateRoomRequest → [InRoom (Host)]
[Connected] → JoinRoomRequest  → [InRoom]
[Connected] → RoomListRequest  → [Connected] (목록 응답 후 상태 유지)

[InRoom] → ToggleReadyRequest → [Ready / NotReady] (토글)
[InRoom] → LeaveRoomRequest   → [Connected]
[InRoom] → 연결 끊김           → [Removed] (방에서 자동 퇴장)

[InRoom (Host)] → StartGameRequest (전원 준비 완료) → [GameStarting]
[InRoom (Host)] → StartGameRequest (미준비 존재)    → RejectResponse

[GameStarting] → GameStart 전송 → [InGame]
[InGame] → SlotReleased 수신   → [Ended] (세션 정리)
```

호스트는 준비 상태와 무관하게 항상 시작 가능 (호스트 자신은 준비 불필요). 호스트 외 전원이 준비 완료일 때만 `StartGameRequest`가 성공한다.

---

## 룸 흐름

### 방 생성 → 참가 → 게임 시작

```
[Client A] ──CreateRoomRequest("My Room", max=4)──► [룸 서버]
                                                        │ Room 생성, A = 호스트
                                                        └── CreateRoomResponse(room) → A

[Client B] ──RoomListRequest──► [룸 서버]
                                    └── RoomListResponse(rooms[]) → B

[Client B] ──JoinRoomRequest(room_id)──► [룸 서버]
                                              │ B가 방에 참가
                                              ├── JoinRoomResponse(room) → B
                                              └── RoomUpdate(room) → A (브로드캐스트)

[Client C] ──JoinRoomRequest(room_id)──► [룸 서버]
                                              │ C가 방에 참가
                                              ├── JoinRoomResponse(room) → C
                                              └── RoomUpdate(room) → A, B (브로드캐스트)

[Client B] ──ToggleReadyRequest──► [룸 서버]
                                        └── RoomUpdate(room: B.is_ready=true) → A, B, C

[Client C] ──ToggleReadyRequest──► [룸 서버]
                                        └── RoomUpdate(room: C.is_ready=true) → A, B, C

[Client A (호스트)] ──StartGameRequest──► [룸 서버]
                                              │ 전원 준비 확인
                                              │ 토큰 생성 (A, B, C 각각)
                                              │ Redis에 토큰 + 세션 정보 저장
                                              │ Room 상태 → IN_GAME
                                              ├── GameStart(session_id, token_A, host, port) → A
                                              ├── GameStart(session_id, token_B, host, port) → B
                                              └── GameStart(session_id, token_C, host, port) → C

[Client A] ──token_A──► [게임 서버 :7979]
[Client B] ──token_B──► [게임 서버 :7979]
[Client C] ──token_C──► [게임 서버 :7979]
```

### 호스트 이탈 처리

```
호스트가 방에서 나감 (LeaveRoomRequest 또는 연결 끊김):
  → 방 즉시 삭제
  → 방에 남아있던 모든 플레이어에게 RejectResponse(ROOM_CLOSED) 전송
  → 플레이어 상태 → [Connected]

사유: 강퇴 기능이 없으므로, 호스트가 원치 않는 플레이어가 있으면 방을 나가고 재생성한다.
이 설계가 강퇴 로직보다 단순하며, PvE 협동 게임 특성상 호스트-게스트 갈등이 적다.
```

### 방 목록 관리

```
[Client] ──RoomListRequest──► [룸 서버]
                                   │ WAITING 상태인 방만 필터링
                                   └── RoomListResponse(rooms[]) → Client

방 목록에 포함되는 정보 (RoomInfo):
  - room_id, room_name, host_id
  - max_players, 현재 인원 (players.size())
  - state (WAITING만 목록에 노출, IN_GAME은 제외)
```

---

## 구축 단계

### Step 1: 프로젝트 스캐폴딩

**목표**: CMake + vcpkg 빌드 환경 구성, Hello World 수준 빌드 확인.

```
작업:
1. proto/room.proto 작성 (package sos.room, 전체 메시지 정의)
2. CMakeLists.txt 작성 (C++20, vcpkg 툴체인, protoc 코드젠)
3. CMakePresets.json 작성 (vcpkg 통합)
4. vcpkg.json 작성 (의존성 선언)
5. src/room/main.cpp 작성 (spdlog로 "Room Server starting..." 출력)
6. 빌드 확인: cmake --preset=default && cmake --build build
```

**완료 기준**: 빌드 성공, 실행 시 로그 출력, protoc 코드젠 정상 동작.

**완료됨.** common 라이브러리(`Codec<T>`, `RedisClient`, `RateLimiter`, `Config`, `Logger`) 포함.

---

### Step 2: TCP 서버 기본 골격

**목표**: Boost.Asio로 TCP 서버 구동, 클라이언트 접속/해제 처리.

```
작업:
1. RoomServer 클래스: io_context + tcp::acceptor (포트 8080)
2. ClientSession 클래스: async_read/async_write 루프
3. 접속 시 spdlog로 IP/포트 로깅
4. 연결 끊김 감지 (EOF, error)
5. Graceful shutdown (SIGINT/SIGTERM 핸들링)
```

**완료됨.** `RoomServer` (Boost.Asio TCP acceptor :8080), `ClientSession` (async_read/write + Codec 통합 + Envelope 디스패치), SIGINT/SIGTERM 시그널 핸들링.

#### 핵심 설계

```
io_context (싱글 스레드 또는 스레드 풀)
    │
    ├── tcp::acceptor::async_accept()
    │       └── ClientSession 생성 → shared_ptr로 수명 관리
    │
    └── ClientSession
            ├── async_read() → 메시지 수신
            ├── async_write() → 메시지 송신
            └── deadline_timer → 하트비트 타임아웃
```

---

### Step 3: Protobuf 프로토콜 통합

**목표**: `room.proto` 기반 Envelope 메시지 송수신 + TCP 프레이밍.

> 메시지 정의: [`room.proto`](../../../proto/room.proto) 참조. 이 파일이 프로토콜의 단일 원본.

```
작업:
1. CMakeLists.txt에 protoc 코드젠 통합 (proto/ → src/protocol/generated/)
2. Codec 클래스: TCP 프레이밍
   - 송신: Envelope.SerializeToString() → [4byte 길이 LE] + [바이너리]
   - 수신: 길이 읽기 → 바이너리 읽기 → Envelope.ParseFromString()
   - 불완전 수신 처리 (TCP 스트림 특성상 메시지가 잘려서 올 수 있음)
3. ClientSession에 Codec 통합
   - 수신 시: Envelope.payload_case()로 oneof 분기
   - 송신 시: 해당 메시지를 Envelope에 감싸서 전송
4. Catch2 테스트
```

**프레이밍 포맷**:
```
[4 bytes: payload length (little-endian)] [N bytes: Envelope protobuf binary]
```

Envelope의 oneof로 메시지 타입을 자동 구분. 별도 MessageType enum이나 타입 헤더 불필요.

**테스트**:
- **유닛**: `test_codec.cpp` — Envelope 각 oneof 케이스별 직렬화/역직렬화 라운드트립, 빈 Envelope, 최대 크기 메시지, 잘못된 바이너리 입력 시 에러 처리
- **피처**: `test_client_connect.cpp` — 실제 TCP 접속 후 Envelope 메시지 송수신 확인

**완료됨.** `ClientSession`에서 `Codec<Envelope>` 통합, `payload_case()` 기반 oneof 디스패치. 테스트는 Phase 6에서 진행.

---

### Step 4: 룸 관리 로직 구현

**목표**: 방 생성/참가/퇴장/준비/시작 로직, 방 목록 조회, 상태 변경 브로드캐스트.

```
작업:
1. Room 클래스:
   - room_id: UUID, room_name, host_id, max_players (1~8)
   - players: std::vector<PlayerInfo> (player_id, player_name, is_ready, is_host)
   - state: RoomState (WAITING / IN_GAME)
   - AddPlayer(player_id, player_name) → 인원 초과 시 실패
   - RemovePlayer(player_id) → 호스트 이탈 시 방 삭제 트리거
   - ToggleReady(player_id) → is_ready 토글 (호스트는 토글 불가, 항상 ready 취급)
   - CanStart() → 호스트 외 전원 is_ready == true
   - BroadcastUpdate() → 방 내 전원에게 RoomUpdate 전송
2. RoomManager 클래스:
   - std::unordered_map<room_id, Room>: 전체 방 관리
   - CreateRoom(player_id, player_name, room_name, max_players) → Room 생성, 호스트 설정
   - JoinRoom(player_id, player_name, room_id) → 방 참가
   - LeaveRoom(player_id) → 방 퇴장 (호스트면 방 삭제)
   - GetRoomList() → WAITING 상태 방 목록 반환
   - FindRoomByPlayer(player_id) → 플레이어가 속한 방 검색
   - 동일 player_id 중복 참가 → RejectResponse(DUPLICATE_PLAYER)
   - 이미 방에 있는 플레이어가 다른 방 참가 시도 → RejectResponse(ALREADY_IN_ROOM)
3. 상태 브로드캐스트:
   - 참가/퇴장/준비 변경 시 방 내 전원에게 RoomUpdate 전송
   - 방 생성/삭제 시 방 목록 갱신 (RoomListRequest를 보낸 클라이언트에게 응답)
4. 호스트 이탈 처리:
   - 호스트 LeaveRoom 또는 연결 끊김 → 방 삭제
   - 방 내 나머지 플레이어에게 RejectResponse(ROOM_CLOSED) 전송
5. 게임 시작:
   - 호스트만 StartGameRequest 가능
   - CanStart() 확인 → 실패 시 RejectResponse(NOT_ALL_READY)
   - 성공 시: 각 플레이어별 토큰 생성 → GameStart 전송
```

#### 주요 거부 사유 (RejectResponse reason)

| reason | 상황 |
|--------|------|
| `DUPLICATE_PLAYER` | 동일 player_id가 이미 서버에 접속 |
| `ALREADY_IN_ROOM` | 이미 다른 방에 참가 중 |
| `ROOM_NOT_FOUND` | 존재하지 않는 room_id |
| `ROOM_FULL` | 방 인원 초과 (max_players 도달) |
| `ROOM_IN_GAME` | 이미 게임 중인 방에 참가 시도 |
| `NOT_HOST` | 호스트가 아닌 플레이어가 StartGameRequest |
| `NOT_ALL_READY` | 전원 준비 미완료 상태에서 게임 시작 시도 |
| `ROOM_CLOSED` | 호스트 이탈로 방 삭제 |
| `RATE_LIMITED` | 요청 빈도 초과 |

**테스트**:
- **유닛**: `test_room.cpp`
  - 방 생성 시 호스트 자동 추가, is_host=true
  - 플레이어 참가 → players 증가, 브로드캐스트 대상 확인
  - max_players 초과 참가 시도 → 실패
  - 준비 토글 → is_ready 상태 변경
  - CanStart(): 전원 준비 → true, 1명이라도 미준비 → false
  - 호스트 이탈 → 방 삭제 트리거
  - 일반 플레이어 이탈 → 방 유지, 해당 플레이어만 제거
- **유닛**: `test_room_manager.cpp`
  - 방 생성 → 방 목록에 추가
  - 방 참가 → 해당 방 인원 증가
  - 방 삭제 → 방 목록에서 제거
  - WAITING 상태만 방 목록에 노출
  - IN_GAME 상태 방은 목록에서 제외
  - 동일 player_id 중복 참가 → 거부
  - 존재하지 않는 room_id → 거부
- **피처**: `test_room_flow.cpp`
  - 3개 TCP 클라이언트: A가 방 생성 → B, C 참가 → 전원 준비 → A가 시작 → GameStart 3명 수신
  - 방 참가 시 기존 멤버에게 RoomUpdate 브로드캐스트 확인
  - 호스트 퇴장 시 방 내 전원에게 ROOM_CLOSED 수신
- **피처**: `test_room_list.cpp`
  - RoomListRequest → 현재 WAITING 상태 방 목록 수신
  - 방 생성/삭제 후 재조회 → 목록 반영 확인
  - IN_GAME 상태 방은 목록에서 제외 확인

**완료됨.** `Room` (생성/입장/퇴장/준비/시작/호스트이탈), `RoomManager` (방 CRUD + 브로드캐스트 + 방 목록 페이징), `UuidGenerator` (UUID v4). 테스트는 Phase 6에서 진행.

---

### Step 5: Redis 연동 — 세션/토큰

**목표**: 게임 시작 시 토큰을 Redis에 저장하고 TTL로 자동 만료 처리.

```
작업:
1. RedisClient 클래스: redis-plus-plus 연결 (config에서 주소 읽기)
2. SessionStore 클래스:
   - CreateToken(playerId, sessionId) → UUID → Redis SET(token:{uuid}, {playerId, sessionId}, EX 60초)
   - ValidateToken(token) → Redis GET → 존재하면 유효 + DEL (일회용)
   - SaveRoomSession(roomId, roomInfo) → Redis HSET(room:{roomId}, ...)
   - GetActiveCount() → Redis SCARD("active_sessions")
3. TokenGenerator: UUID v4 생성 (랜덤)
4. 게임 시작 시: 각 플레이어별 토큰 생성 → Redis 저장 → GameStart에 포함
5. 게임 서버 토큰 검증 시: TokenValidateRequest 수신 → Redis 조회 → 응답
```

#### Redis 키 구조

```
token:{uuid}              → {playerId, sessionId}  (STRING, TTL 60초)  : 입장 토큰
room:{roomId}             → {room_name, host_id, max_players, state, players_json}
                                                     (HASH)             : 방 정보
active_sessions           → {sessionId, ...}        (SET)              : 현재 진행 중인 게임 세션
game_server:{serverId}    → last_heartbeat_ts       (STRING, TTL 90초) : 게임 서버 하트비트
rate:{ip}                 → count                   (STRING, TTL 10초) : Rate Limit 카운터
```


#### 비정상 종료 시 슬롯 회수

```
게임 서버가 정상 종료: SlotReleased 메시지 → 즉시 세션 정리
게임 서버가 비정상 종료 (크래시, 네트워크 단절):
  1. GameServerHeartbeat가 30초 간격으로 수신됨
  2. 룸 서버는 game_server:{serverId} 키를 TTL 90초로 갱신
  3. 90초간 하트비트 미수신 → 키 만료 → 해당 서버의 모든 세션을 Ended로 전환
  4. active_sessions에서 제거 → Room 상태 초기화 → 클라이언트에 알림
```

**테스트**:
- **유닛**: `test_token_generator.cpp`
  - UUID v4 포맷 검증 (8-4-4-4-12)
  - 1000개 생성 시 유일성 보장
- **피처**: `test_token_validate.cpp`
  - 토큰 생성 → Redis에 존재 확인 (GET)
  - 토큰 검증 → 유효 + DEL (일회용: 재검증 시 실패)
  - TTL 60초 → 대기 후 검증 시 만료 확인
  - 존재하지 않는 토큰 검증 → 무효 응답

**완료 기준**: 토큰 발급 → 60초 내 검증 성공 → 60초 후 검증 실패 (TTL 만료).

---

### Step 6: Redis 연동 — Rate Limiting

**목표**: 클라이언트 IP 기반 요청 제한으로 남용 방지.

```
작업:
1. RateLimiter 클래스:
   - CheckRate(ip) → Redis INCR("rate:{ip}") + EXPIRE 10초
   - 10초 내 요청 횟수가 임계값(예: 20회) 초과 시 거부
2. ClientSession에 통합: 메시지 수신 시 RateLimiter.CheckRate() 선행
3. 초과 시 RejectResponse(RATE_LIMITED) 전송 + 연결 종료
```

**테스트**:
- **유닛**: `test_ratelimit_logic.cpp`
  - 카운터 증가 로직 (Redis mock 또는 인터페이스)
  - 임계값 미만 → 허용, 임계값 도달 → 거부, 윈도우 초과 후 → 리셋
- **피처**: `test_ratelimit_redis.cpp`
  - 실제 Redis 연동: 20회 요청 허용 → 21번째 거부
  - 10초 경과 후 카운터 리셋 확인
  - 서로 다른 IP → 독립적 카운터

**완료 기준**: 빠른 연속 요청 시 제한 동작 확인.

---

### Step 7: 게임 서버 내부 통신 채널

**목표**: 게임 서버(:7979)와 룸 서버 간 직접 TCP 채널(:8081). 토큰 검증 + 슬롯 반환 + 하트비트.

```
작업:
1. GameServerChannel 클래스: 내부 포트 :8081에서 게임 서버 연결 수락
2. 토큰 검증: TokenValidateRequest 수신 → Redis 조회 → TokenValidateResponse 응답
3. 슬롯 반환: SlotReleased 수신 → active_sessions에서 제거 → Room을 Ended로 전환
4. 하트비트: GameServerHeartbeat 수신 → game_server:{serverId} TTL 갱신
5. 내부 통신이므로 인증 불필요, localhost 바인딩
6. 게임 서버 연결 끊김 감지 → 해당 서버 세션 전체를 TTL 기반 만료 대기
```

직접 TCP를 선택한 이유: 게임 서버와 룸 서버가 같은 머신에서 동작하며,
이미 Protobuf Envelope이 정의된 TCP 채널이 있으므로 Redis Pub/Sub를 경유할 이유가 없음.
SlotReleased + GameServerHeartbeat 모두 이 채널에서 처리하여 단일 통신 경로로 통합.

**테스트**:
- **피처**: `test_internal_channel.cpp`
  - 내부 채널 접속 → TokenValidateRequest → 유효 토큰 검증 성공
  - 내부 채널 접속 → TokenValidateRequest → 무효 토큰 검증 실패
  - SlotReleased 전송 → active_sessions에서 세션 제거 확인
  - GameServerHeartbeat 전송 → TTL 갱신 확인
  - 하트비트 미전송 → TTL 만료 후 세션 자동 정리
- **피처**: `test_edge_cases.cpp`
  - 호스트 이탈 → 방 삭제, 나머지 플레이어에게 ROOM_CLOSED
  - 게임 시작 직전 플레이어 이탈 → 인원 변경, 준비 상태 재확인 필요
  - 동일 player_id로 중복 CreateRoomRequest → RejectResponse(DUPLICATE_PLAYER)
  - 동시에 여러 클라이언트가 같은 방에 JoinRoomRequest → race condition 없음
  - 만석 방에 JoinRoomRequest → RejectResponse(ROOM_FULL)
  - 내부 채널 연결 끊김 → 룸 서버 정상 동작 유지, 해당 세션 TTL 만료 대기
  - 게임 서버 비정상 종료 → 90초 후 세션 자동 정리 + 슬롯 복구

**완료 기준**: 토큰 검증, 슬롯 반환, 하트비트, 비정상 종료 복구 모두 동작.

---

### Step 8: Unity 연동 (Project-SOS 측 작업)

**목표**: Unity 클라이언트 + 게임 서버에서 룸 서버와 통신하는 코드 구현.

```
작업:
1. GameBootStrap.cs: AutoConnectPort = 0 (자동 연결 비활성화)
2. Protobuf 환경: room.proto + protoc C# 코드젠
3. RoomClient.cs: TCP async 클라이언트 (룸 서버 :8080, DontDestroyOnLoad)
4. RoomAuthState.cs: 토큰 전달용 ECS 싱글톤
5. GoInGameRequestRpc.cs: AuthToken 필드 추가
6. RoomSessionInfo.cs: session_id + player_id 저장 (Connection 엔티티에 부착)
7. TokenValidationSystem: managed SystemBase, 토큰 검증 → Tag + SessionInfo 부착
8. GoInGameServerSystem.cs: .WithAll<TokenValidatedTag>() 추가
9. RoomTokenValidator.cs: 룸 서버 :8081 TCP 연결 1 (토큰 검증 요청-응답)
10. SlotNotifyClient.cs: 룸 서버 :8081 TCP 연결 2 (SlotReleased + Heartbeat, 별도 연결)
11. SlotNotifySystem: 연결 끊김 시 RoomSessionInfo에서 session_id 읽어 SlotReleased 전송
12. RoomUI.cs: 방 목록 / 방 생성 / 대기실 / 준비 상태 화면

전체 흐름 (session_id / auth_token):
  룸 서버: GameStart(session_id, auth_token, host, port) → 클라이언트
  클라이언트: auth_token을 GoInGameRequestRpc에 담아 게임 서버 전송
  TokenValidationSystem: auth_token → 룸 서버 검증(:8081, TCP 연결 1)
    → TokenValidateResponse(valid, player_id, session_id) 수신
    → Connection에 RoomSessionInfo(session_id, player_id) 부착
  GoInGameServerSystem: TokenValidatedTag 있는 RPC → Hero 생성
  게임 종료/연결 끊김:
    SlotNotifySystem: RoomSessionInfo에서 session_id, player_id 읽기
    → SlotReleased(player_id, session_id) 전송 (:8081, TCP 연결 2)
```

**완료 기준**: 1~8명의 Unity 클라이언트가 룸 서버를 거쳐 방 생성/참가 → 게임 서버 입장 → 게임 플레이까지 전체 흐름 동작.

---

### Step 9: 설정 파일 및 Graceful Shutdown

**목표**: 하드코딩 제거, 운영 가능한 수준의 설정 관리.

```
작업:
1. server_config.json 작성:
   {
     "room_port": 8080,
     "internal_port": 8081,
     "max_rooms": 100,
     "max_players_per_room": 8,
     "redis_host": "127.0.0.1",
     "redis_port": 6379,
     "token_ttl_seconds": 60,
     "heartbeat_timeout_seconds": 30,
     "game_server_heartbeat_ttl_seconds": 90,
     "rate_limit_max": 20,
     "rate_limit_window_seconds": 10,
     "game_server_host": "127.0.0.1",
     "game_server_port": 7979
   }
2. Config 클래스: nlohmann/json으로 파싱
3. Graceful shutdown:
   - SIGINT → io_context.stop()
   - 현재 방 상태 로깅
   - Redis 연결 정리
   - 모든 ClientSession에 서버 종료 알림 후 close
```

**완료 기준**: 설정 변경 후 재시작 → 반영 확인. Ctrl+C → 클린 종료.

---

### Step 10: 부하 테스트

**목표**: 정량적 성능 지표 확보. 포트폴리오에 수치로 제시.

```
작업:
1. loadtest/load_client.py 작성:
   - Python asyncio + socket 기반 더미 클라이언트
   - Protobuf Envelope 메시지 생성/파싱 (protobuf Python 패키지)
   - 시나리오:
     · 동시 N개 방 생성 + 방당 최대 인원 참가
     · 방 목록 조회 폭주 (RoomListRequest 대량 전송)
     · 전원 준비 → 게임 시작 응답 시간
   - 측정 항목:
     · CreateRoomRequest → CreateRoomResponse 응답 시간 (p50, p95, p99)
     · RoomListRequest → RoomListResponse 응답 시간
     · StartGameRequest → GameStart 전파 시간
     · 동시 100/500/1000명 처리 시 서버 CPU/메모리
     · Rate Limit 동작 검증 (초당 요청 폭주)
     · 연결 끊김 후 방 정리 시간
2. 결과 기록: docs/benchmark.md
   - 테스트 환경 (CPU, RAM, OS)
   - 시나리오별 수치 테이블
   - 병목 분석 및 개선 여지

측정 예시:
  동시 50개 방 (방당 8명 = 400명) → 게임 시작 평균 응답 시간: Xms
  동시 500명 방 목록 조회 → 평균 응답 시간: Xms
  Rate Limit (20req/10s) → 초과 시 거부율: 100%
```

**완료 기준**: benchmark.md에 재현 가능한 수치 기록 완료.

---

### Step 11: 통합 테스트 및 문서화

**목표**: 전체 흐름 검증 + 포트폴리오용 문서 작성.

```
작업:
1. 통합 테스트 시나리오:
   - 방 생성 → 참가 → 전원 준비 → 게임 시작 → 토큰 발급 → 게임 서버 입장
   - 방 목록 조회 → 방 참가 → 게임 플레이
   - 방에서 퇴장 → 방 상태 갱신 → 브로드캐스트 확인
   - 호스트 이탈 → 방 삭제 → 나머지 클라이언트 ROOM_CLOSED 수신
   - 만석 방 참가 시도 → ROOM_FULL
   - 미준비 상태에서 게임 시작 시도 → NOT_ALL_READY
   - 토큰 만료 후 게임 서버 접속 시도 → 거부
   - Rate Limit 초과 → 거부
   - 게임 종료 → SlotReleased → 세션 정리
   - 게임 서버 비정상 종료 → 하트비트 TTL 만료 → 슬롯 자동 복구
2. docs/architecture.md 작성:
   - 시스템 아키텍처 다이어그램
   - 메시지 프로토콜 명세 (room.proto 참조)
   - Redis 키 설계
   - 시퀀스 다이어그램 (방 생성 → 참가 → 준비 → 시작 → 입장 → 게임 → 종료)
   - 비정상 종료 복구 흐름
   - 부하 테스트 결과 요약
3. README.md 작성:
   - 빌드 방법 (CMake + vcpkg)
   - 실행 방법 (Redis + 룸 서버)
   - 설정 설명
   - Project-SOS(Unity) 연동 안내 링크
```

**완료 기준**: 전체 통합 시나리오 통과. README대로 클론 → 빌드 → 실행 가능.

---

## 테스트 전략

### 유닛 테스트 vs 피처 테스트 구분

| 구분 | 유닛 테스트 (`tests/unit/`) | 피처 테스트 (`tests/feature/`) |
|------|---------------------------|-------------------------------|
| **외부 의존** | 없음 (순수 로직) | Redis, TCP 소켓 필요 |
| **실행 속도** | 밀리초 단위 | 초 단위 (네트워크/Redis 왕복) |
| **실행 조건** | 빌드만 하면 실행 가능 | Redis 실행 + 포트 개방 필요 |
| **CMake 타겟** | `unit-tests` | `feature-tests` |
| **CI 단계** | 매 커밋 | PR 머지 전 / 수동 |

### 테스트 매트릭스

| 테스트 파일 | 구분 | 대상 Step | 검증 내용 |
|------------|------|-----------|----------|
| `test_room.cpp` | 유닛 | Step 4 | 방 참가/퇴장/준비/시작/호스트 이탈/인원 제한 |
| `test_room_manager.cpp` | 유닛 | Step 4 | 방 생성/삭제/목록 조회/중복 방지/상태 필터링 |
| `test_token_generator.cpp` | 유닛 | Step 5 | UUID 포맷, 유일성 |
| `test_codec.cpp` | 유닛 | Step 3 | Envelope oneof 직렬화 라운드트립, 에러 입력 |
| `test_config.cpp` | 유닛 | Step 9 | JSON 파싱, 기본값, 누락 필드 |
| `test_ratelimit_logic.cpp` | 유닛 | Step 6 | 카운터 로직 (Redis mock) |
| `test_client_connect.cpp` | 피처 | Step 2~3 | TCP 접속/해제/하트비트 타임아웃/Envelope 송수신 |
| `test_room_flow.cpp` | 피처 | Step 4 | 방 생성→참가→준비→시작 E2E, 브로드캐스트 |
| `test_room_list.cpp` | 피처 | Step 4 | 방 목록 조회, 상태 필터링, 실시간 갱신 |
| `test_token_validate.cpp` | 피처 | Step 5 | Redis 토큰 TTL, 일회용, 만료 |
| `test_ratelimit_redis.cpp` | 피처 | Step 6 | Redis Rate Limit 실동작, IP별 독립 |
| `test_internal_channel.cpp` | 피처 | Step 7 | 토큰 검증, SlotReleased, 하트비트, TTL 만료 |
| `test_edge_cases.cpp` | 피처 | Step 7 | 호스트 이탈, 동시 참가, 만석 방, 비정상 종료 복구 |

### 빌드 및 실행

```bash
# 유닛 테스트만 (Redis 불필요)
cmake --build build --target unit-tests
./build/tests/unit-tests

# 피처 테스트 (Redis 실행 필요)
docker run -d --name redis -p 6379:6379 redis:7-alpine
cmake --build build --target feature-tests
./build/tests/feature-tests

# 전체
cmake --build build --target all-tests
```

---

## 단계별 의존성

```
[Step 1] 프로젝트 스캐폴딩 + room.proto 확정
    │
    ▼
[Step 2] TCP 서버 골격 ──────────────┐
    │                                │
    ▼                                ▼
[Step 3] Protobuf Envelope 통합  (Redis 설치/Docker)
    │                                │
    ▼                                │
[Step 4] 룸 관리 로직 ◄──────────────┘
    │
    ├──► [Step 5] Redis 세션/토큰
    │        │
    │        └──► [Step 6] Rate Limiting
    │
    └──► [Step 7] 내부 TCP 채널 (토큰 검증 + SlotReleased + 하트비트)
              │
              ▼
         [Step 8] Unity 연동 (Project-SOS 측)
              │
              ▼
         [Step 9] 설정 파일 + Graceful Shutdown
              │
              ├──► [Step 10] 부하 테스트
              │
              └──► [Step 11] 통합 테스트 + 문서화
```

- Step 1~4: 순차 (각 단계가 이전 결과에 의존)
- Step 5~6: Step 4 완료 후 진행 (Redis 관련)
- Step 7: Step 3(프로토콜) + Step 5(토큰) 완료 후 시작
- Step 8: Step 7 완료 후 시작 (내부 채널이 있어야 Unity 검증 가능)
- Step 10, 11: Step 9 완료 후 병렬 진행 가능

---

## 예상 산출물

| 산출물 | 설명 |
|--------|------|
| **실행 바이너리** | `room-server.exe` (Windows) — 룸 서버 |
| **설정 파일** | `server_config.json` |
| **Protobuf 스키마** | `room.proto` (package sos.room, C++/C# 공유) |
| **테스트 바이너리** | `unit-tests.exe`, `feature-tests.exe` (Catch2) |
| **부하 테스트** | `load_client.py` + `docs/benchmark.md` |
| **문서** | `README.md`, `docs/architecture.md` |
| **Docker Compose** | Redis + 룸 서버 일괄 실행 (선택) |
