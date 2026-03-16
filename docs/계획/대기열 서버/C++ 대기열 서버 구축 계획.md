# C++ 매칭/대기열 서버 구축 계획

> **목적**: Unity 게임 서버(:7979) 앞단에 위치하여 클라이언트 매칭 및 접속을 제어하는 서버 구축.
> 2인 매칭 큐 → 게임 세션 생성 → 양쪽 클라이언트에 접속 정보 전달 → 토큰 기반 입장 검증.
> **저장소**: `Project-SOS-QueueServer` (별도 Git 저장소)
> **프로토콜 정의**: [`queue.proto`](queue.proto) (Single Source of Truth, 양쪽 문서 공통 참조)

---

## 기술 스택

| 항목 | 선택 | 버전 | 용도 |
|------|------|------|------|
| 언어 | C++20 | MSVC 17+ | coroutine, concepts |
| 빌드 | CMake | 3.25+ | 빌드 시스템 |
| 패키지 | vcpkg | latest | 라이브러리 관리 |
| 비동기 I/O | Boost.Asio | 1.84+ | TCP 서버, 비동기 소켓 |
| Redis 클라이언트 | hiredis + redis-plus-plus | 1.2+ / 1.3+ | 세션 TTL, Rate Limit |
| 직렬화 | Protobuf | 3.25+ | 스키마 기반 메시지, C++/C# 코드젠 |
| 로깅 | spdlog | 1.13+ | 구조화된 로그 출력 |
| 테스트 | Catch2 | 3.5+ | 단위/통합 테스트 |
| 외부 서비스 | Redis | 7.0+ | 인메모리 데이터 스토어 |

### 직렬화: Protobuf 선택 근거
- `.proto` 파일 하나로 C++(큐 서버) / C#(Unity 클라이언트, 게임 서버) 양쪽 코드젠
- 게임 업계 표준 직렬화 포맷 (gRPC 기반 경험과 연결)
- 강타입 스키마로 프로토콜 버전 관리 용이
- Envelope oneof 패턴으로 메시지 타입 디스패치 — 별도 타입 헤더 불필요

---

## 준비물

### 1. 개발 환경

| 항목 | 설치 방법 | 비고 |
|------|----------|------|
| **Visual Studio 2022** | [설치](https://visualstudio.microsoft.com/) | "C++ 데스크톱 개발" 워크로드 선택 |
| **CMake 3.25+** | VS 포함 또는 [별도 설치](https://cmake.org/) | `cmake --version`으로 확인 |
| **vcpkg** | `git clone https://github.com/microsoft/vcpkg` → `bootstrap-vcpkg.bat` | 환경변수 `VCPKG_ROOT` 설정 |
| **Git** | 기존 설치 활용 | 저장소 관리 |
| **Docker Desktop** | [설치](https://www.docker.com/products/docker-desktop/) | Redis 실행용 |

### 2. Redis 서버

| 방법 | 명령 | 비고 |
|------|------|------|
| **Docker (권장)** | `docker run -d --name redis -p 6379:6379 redis:7-alpine` | 가장 간편, Windows 호환 |
| WSL2 | `sudo apt install redis-server` | WSL 내부에서 실행 |
| Memurai | [다운로드](https://www.memurai.com/) | Windows 네이티브 Redis 호환 |

Docker 방식 권장. RedisInsight로 데이터 확인 가능.

### 3. vcpkg 의존성

```json
{
  "name": "project-sos-queue-server",
  "version": "0.1.0",
  "dependencies": [
    "boost-asio",
    "hiredis",
    "redis-plus-plus",
    "protobuf",
    "nlohmann-json",
    "spdlog",
    "catch2"
  ]
}
```

### 4. Protobuf 스키마 공유

```
Project-SOS-Protocol/          # 공유 저장소 (git submodule)
└── proto/
    └── queue.proto            # 메시지 정의 — queue.proto 참조
```

C++ 큐 서버와 Unity 프로젝트 양쪽에서 submodule로 참조.
Unity 측은 `Google.Protobuf` NuGet 패키지 + `protoc`로 C# 코드젠.

---

## 프로젝트 구조

```
Project-SOS-QueueServer/
├── CMakeLists.txt              # 루트 빌드 설정
├── CMakePresets.json           # vcpkg 통합 프리셋
├── vcpkg.json                  # 의존성 선언
├── .gitignore
├── README.md
│
├── proto/                      # submodule (Project-SOS-Protocol)
│   └── queue.proto
│
├── src/
│   ├── main.cpp                # 진입점 (서버 시작)
│   ├── server/
│   │   ├── QueueServer.h/.cpp        # Asio TCP 서버 (accept 루프)
│   │   └── ClientSession.h/.cpp      # 개별 클라이언트 연결 관리
│   ├── match/
│   │   ├── MatchQueue.h/.cpp         # 매칭 대기열 (2인 매칭)
│   │   ├── MatchSession.h/.cpp       # 매칭된 세션 관리
│   │   └── TokenGenerator.h/.cpp     # 인증 토큰 생성
│   ├── redis/
│   │   ├── RedisClient.h/.cpp        # redis-plus-plus 래퍼
│   │   └── SessionStore.h/.cpp       # 세션/토큰 TTL 관리
│   ├── protocol/
│   │   ├── generated/                # protoc 생성 코드 (빌드 시 자동)
│   │   └── Codec.h/.cpp              # 프레이밍 (4byte 길이 접두사 + Envelope)
│   ├── ratelimit/
│   │   └── RateLimiter.h/.cpp        # Redis 기반 요청 제한
│   ├── internal/
│   │   └── GameServerChannel.h/.cpp  # 게임 서버 ↔ 큐 서버 내부 TCP 채널
│   └── util/
│       ├── Config.h/.cpp             # 설정 파일 로딩
│       └── Logger.h                  # spdlog 초기화
│
├── tests/
│   ├── unit/                         # 유닛 테스트 (외부 의존 없음)
│   │   ├── test_match_queue.cpp      # MatchQueue 진입/퇴장/순번/매칭 성립
│   │   ├── test_token_generator.cpp  # UUID 생성, 유일성, 포맷
│   │   ├── test_codec.cpp            # Protobuf Envelope 직렬화/역직렬화
│   │   ├── test_config.cpp           # 설정 파일 파싱, 기본값, 누락 필드
│   │   └── test_ratelimit_logic.cpp  # Rate Limit 카운터 로직 (Redis mock)
│   │
│   ├── feature/                      # 피처 테스트 (실제 TCP/Redis 연동)
│   │   ├── test_client_connect.cpp   # TCP 접속/해제/하트비트 타임아웃
│   │   ├── test_match_flow.cpp       # 2명 접속→매칭→MatchFound 수신 E2E
│   │   ├── test_token_validate.cpp   # 토큰 발급→Redis 저장→검증→만료→일회용
│   │   ├── test_ratelimit_redis.cpp  # Redis 기반 Rate Limit 실동작
│   │   ├── test_internal_channel.cpp # 내부 채널: 토큰 검증 + SlotReleased + 하트비트
│   │   └── test_edge_cases.cpp       # 매칭 중 이탈, 동시 취소, 중복 요청, 비정상 종료
│   │
│   └── CMakeLists.txt                # 테스트 빌드 설정 (unit/feature 분리)
│
├── loadtest/
│   ├── load_client.py          # Python asyncio 부하 테스트 클라이언트
│   └── requirements.txt        # protobuf, asyncio 의존성
│
├── config/
│   └── server_config.json      # 서버 설정
│
└── docs/
    └── architecture.md         # 아키텍처 설명 (포폴용)
```

---

## 구축 단계

### Step 1: 프로젝트 스캐폴딩

**목표**: CMake + vcpkg 빌드 환경 구성, Hello World 수준 빌드 확인.

```
작업:
1. Git 저장소 생성 (Project-SOS-QueueServer)
2. Project-SOS-Protocol 저장소 생성, queue.proto 커밋
3. queue.proto를 submodule로 추가
4. CMakeLists.txt 작성 (C++20, vcpkg 툴체인, protoc 코드젠)
5. CMakePresets.json 작성 (vcpkg 통합)
6. vcpkg.json 작성 (의존성 선언)
7. src/main.cpp 작성 (spdlog로 "Server starting..." 출력)
8. 빌드 확인: cmake --preset=default && cmake --build build
```

**완료 기준**: 빌드 성공, 실행 시 로그 출력, protoc 코드젠 정상 동작.

---

### Step 2: TCP 서버 기본 골격

**목표**: Boost.Asio로 TCP 서버 구동, 클라이언트 접속/해제 처리.

```
작업:
1. QueueServer 클래스: io_context + tcp::acceptor (포트 8080)
2. ClientSession 클래스: async_read/async_write 루프
3. 접속 시 spdlog로 IP/포트 로깅
4. 연결 끊김 감지 (EOF, error)
5. Graceful shutdown (SIGINT/SIGTERM 핸들링)
```

**완료 기준**: telnet/netcat으로 접속 → 로그 확인 → 연결 해제 정상 처리.

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

**목표**: `queue.proto` 기반 Envelope 메시지 송수신 + TCP 프레이밍.

> 메시지 정의: [`queue.proto`](queue.proto) 참조. 이 파일이 프로토콜의 단일 원본.

```
작업:
1. CMakeLists.txt에 protoc 코드젠 통합 (proto/ → src/protocol/generated/)
2. Codec 클래스: TCP 프레이밍
   - 송신: Envelope.SerializeToString() → [4byte 길이] + [바이너리]
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

**완료 기준**: Catch2 테스트 전체 통과.

---

### Step 4: 매칭 대기열 로직 구현

**목표**: 2인 매칭 큐, 세션 생성, 양쪽 클라이언트에 접속 정보 전달.

```
작업:
1. MatchQueue 클래스:
   - std::deque<SessionPtr>: 매칭 대기열
   - TryMatch(): 대기열에 2명 이상 → 매칭 성립
   - 매칭 성립 시 MatchSession 생성 + 양쪽에 MatchFound 전송
   - 동일 player_id 중복 진입 → RejectResponse(DUPLICATE_PLAYER)
2. MatchSession 클래스:
   - sessionId: UUID
   - 매칭된 두 플레이어 정보 (playerId, teamId)
   - 상태: Matched → WaitingForJoin → InGame → Ended
3. 순번 브로드캐스트: 주기적으로 (2초) QueueStatusUpdate 전송
4. 이탈 처리: 대기 중 연결 끊김 → 큐에서 제거 → 뒷순번 갱신
5. 매칭 취소: CancelMatchRequest 수신 → 큐에서 제거
```

#### 매칭 흐름

```
[클라이언트 A] ──MatchRequest──► [큐 서버]
                                     │ 대기열 진입 (순번 1)
                                     │ QueueStatusUpdate(1, 1) → A
[클라이언트 B] ──MatchRequest──► [큐 서버]
                                     │ 대기열 진입 (순번 2)
                                     │ TryMatch() → A, B 매칭 성립
                                     │
                                     ├── MatchFound(token_A, team=1) → A
                                     └── MatchFound(token_B, team=2) → B

[클라이언트 A] ──token_A──► [게임 서버 :7979]
[클라이언트 B] ──token_B──► [게임 서버 :7979]
```

#### 클라이언트 세션 상태 머신

```
[Connected] → MatchRequest → [Queued]
[Queued] → 매칭 성립 → [Matched] → MatchFound 전송
[Queued] → CancelMatchRequest → [Connected]
[Queued] → 연결 끊김 → [Removed]
[Matched] → 게임 서버 접속 확인 (TokenValidate 통과) → [InGame]
[Matched] → 토큰 만료 (60초) → [Expired] → 세션 취소, 상대도 재매칭
[InGame] → SlotReleased 수신 → [Ended] → 세션 정리
```

**테스트**:
- **유닛**: `test_match_queue.cpp`
  - 1명 진입 → 매칭 미성립, 대기열 크기 1
  - 2명 진입 → TryMatch() → 매칭 성립, 대기열 비어있음
  - 3명 진입 → 2명 매칭, 1명 대기
  - 대기 중 이탈 → 큐에서 제거, 뒷순번 갱신
  - CancelMatchRequest → 큐에서 제거
  - 빈 큐에서 TryMatch() → 아무 일도 안 일어남
  - 동일 playerId 중복 진입 → RejectResponse(DUPLICATE_PLAYER)
- **피처**: `test_match_flow.cpp`
  - 2개 TCP 클라이언트 동시 접속 → MatchRequest → MatchFound 양쪽 수신
  - 매칭 후 팀 번호(team_id) 서로 다른지 확인
  - 매칭 대기 중 한쪽 연결 끊김 → 나머지 클라이언트 큐 유지

**완료 기준**: 2명 접속 → 매칭 성립 → 양쪽에 MatchFound 수신 확인.

---

### Step 5: Redis 연동 — 세션/토큰

**목표**: 토큰을 Redis에 저장하고 TTL로 자동 만료 처리.

```
작업:
1. RedisClient 클래스: redis-plus-plus 연결 (config에서 주소 읽기)
2. SessionStore 클래스:
   - CreateToken(playerId, sessionId) → UUID → Redis SET(token, {playerId, sessionId}, EX 60초)
   - ValidateToken(token) → Redis GET → 존재하면 유효 + DEL (일회용)
   - GetActiveCount() → Redis SCARD("active_sessions")
3. TokenGenerator: UUID v4 생성 (랜덤)
4. 매칭 성립 시: 토큰 생성 → Redis 저장 → MatchFound에 포함
5. 게임 서버 토큰 검증 시: TokenValidateRequest 수신 → Redis 조회 → 응답
```

#### Redis 키 구조

```
token:{uuid}              → {playerId, sessionId}  (STRING, TTL 60초)  : 입장 토큰
session:{sessionId}        → {player1, player2, ...} (HASH)             : 매칭 세션 정보
active_sessions            → {sessionId, ...}        (SET)              : 현재 진행 중인 세션
game_server:{serverId}     → last_heartbeat_ts       (STRING, TTL 90초) : 게임 서버 하트비트
rate:{ip}                  → count                   (STRING, TTL 10초) : Rate Limit 카운터
queue:stats                → {json}                  (STRING)           : 대기열 통계 (모니터링)
```

#### 비정상 종료 시 슬롯 회수

```
게임 서버가 정상 종료: SlotReleased 메시지 → 즉시 세션 정리
게임 서버가 비정상 종료 (크래시, 네트워크 단절):
  1. GameServerHeartbeat가 30초 간격으로 수신됨
  2. 큐 서버는 game_server:{serverId} 키를 TTL 90초로 갱신
  3. 90초간 하트비트 미수신 → 키 만료 → 해당 서버의 모든 세션을 Ended로 전환
  4. active_sessions에서 제거 → 슬롯 복구 → 대기열 진행 재개
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

**목표**: 게임 서버(:7979)와 큐 서버 간 직접 TCP 채널(:8081). 토큰 검증 + 슬롯 반환 + 하트비트.

```
작업:
1. GameServerChannel 클래스: 내부 포트 :8081에서 게임 서버 연결 수락
2. 토큰 검증: TokenValidateRequest 수신 → Redis 조회 → TokenValidateResponse 응답
3. 슬롯 반환: SlotReleased 수신 → active_sessions에서 제거 → MatchSession을 Ended로 전환
4. 하트비트: GameServerHeartbeat 수신 → game_server:{serverId} TTL 갱신
5. 내부 통신이므로 인증 불필요, localhost 바인딩
6. 게임 서버 연결 끊김 감지 → 해당 서버 세션 전체를 TTL 기반 만료 대기
```

직접 TCP를 선택한 이유: 게임 서버와 큐 서버가 같은 머신에서 동작하며,
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
  - 매칭 성립 직후 한쪽 연결 끊김 → 세션 취소, 나머지 재매칭
  - 동시에 CancelMatchRequest + 매칭 성립 → race condition 없음
  - 동일 playerId로 중복 MatchRequest → RejectResponse(DUPLICATE_PLAYER)
  - 내부 채널 연결 끊김 → 큐 서버 정상 동작 유지, 해당 세션 TTL 만료 대기
  - 게임 서버 비정상 종료 → 90초 후 세션 자동 정리 + 슬롯 복구

**완료 기준**: 토큰 검증, 슬롯 반환, 하트비트, 비정상 종료 복구 모두 동작.

---

### Step 8: Unity 연동 (Project-SOS 측 작업)

**목표**: Unity 클라이언트 + 게임 서버에서 큐 서버와 통신하는 코드 구현.

> 상세 내용은 [대기열 서버 연동 계획.md](대기열%20서버%20연동%20계획.md) 참조.

```
작업:
1. GameBootStrap.cs: AutoConnectPort = 0 (자동 연결 비활성화)
2. Protobuf 환경: queue.proto submodule + protoc C# 코드젠
3. QueueClient.cs: TCP async 클라이언트 (큐 서버 :8080, DontDestroyOnLoad)
4. QueueAuthState.cs: 토큰 전달용 ECS 싱글톤
5. GoInGameRequestRpc.cs: AuthToken 필드 추가
6. QueueSessionInfo.cs: session_id + player_id 저장 (Connection 엔티티에 부착)
7. TokenValidationSystem: managed SystemBase, 토큰 검증 → Tag + SessionInfo 부착
8. GoInGameServerSystem.cs: .WithAll<TokenValidatedTag>() 추가
9. QueueTokenValidator.cs: 큐 서버 :8081 TCP 연결 1 (토큰 검증 요청-응답)
10. SlotNotifyClient.cs: 큐 서버 :8081 TCP 연결 2 (SlotReleased + Heartbeat, 별도 연결)
11. SlotNotifySystem: 연결 끊김 시 QueueSessionInfo에서 session_id 읽어 SlotReleased 전송
12. QueueUI.cs: 매칭/대기열 상태 화면

전체 흐름 (session_id / auth_token):
  큐 서버: 매칭 성립 → MatchFound(session_id, auth_token) → 클라이언트
  클라이언트: auth_token을 GoInGameRequestRpc에 담아 게임 서버 전송
  TokenValidationSystem: auth_token → 큐 서버 검증(:8081, TCP 연결 1)
    → TokenValidateResponse(valid, player_id, session_id) 수신
    → Connection에 QueueSessionInfo(session_id, player_id) 부착
  GoInGameServerSystem: TokenValidatedTag 있는 RPC → Hero 생성
  게임 종료/연결 끊김:
    SlotNotifySystem: QueueSessionInfo에서 session_id, player_id 읽기
    → SlotReleased(player_id, session_id) 전송 (:8081, TCP 연결 2)
```

**완료 기준**: 2명의 Unity 클라이언트가 큐 서버를 거쳐 매칭 → 게임 서버 입장 → 게임 플레이까지 전체 흐름 동작.

---

### Step 9: 설정 파일 및 Graceful Shutdown

**목표**: 하드코딩 제거, 운영 가능한 수준의 설정 관리.

```
작업:
1. server_config.json 작성:
   {
     "queue_port": 8080,
     "internal_port": 8081,
     "match_size": 2,
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
   - 대기열 상태 로깅
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
   - 동시 접속 N명 시뮬레이션
   - 측정 항목:
     · MatchRequest → MatchFound 수신까지 응답 시간 (p50, p95, p99)
     · 동시 접속 100/500/1000명 처리 시 서버 CPU/메모리
     · Rate Limit 동작 검증 (초당 요청 폭주)
     · 연결 끊김 후 큐 정리 시간
2. 결과 기록: docs/benchmark.md
   - 테스트 환경 (CPU, RAM, OS)
   - 시나리오별 수치 테이블
   - 병목 분석 및 개선 여지

측정 예시:
  동시 100명 매칭 요청 → 평균 응답 시간: Xms
  동시 500명 대기열 유지 → 메모리 사용량: XMB
  Rate Limit (20req/10s) → 초과 시 거부율: 100%
```

**완료 기준**: benchmark.md에 재현 가능한 수치 기록 완료.

---

### Step 11: 통합 테스트 및 문서화

**목표**: 전체 흐름 검증 + 포트폴리오용 문서 작성.

```
작업:
1. 통합 테스트 시나리오:
   - 2명 접속 → 매칭 성립 → 토큰 발급 → 게임 서버 입장 → 게임 플레이
   - 1명 대기 중 → 2번째 접속 → 매칭 → 양쪽 MatchFound 수신
   - 매칭 대기 중 취소 → 큐에서 제거 → 뒷순번 갱신
   - 토큰 만료 후 게임 서버 접속 시도 → 거부
   - Rate Limit 초과 → 거부
   - 대기 중 연결 끊김 → 큐 정리
   - 게임 종료 → SlotReleased → 세션 정리
   - 게임 서버 비정상 종료 → 하트비트 TTL 만료 → 슬롯 자동 복구
2. docs/architecture.md 작성:
   - 시스템 아키텍처 다이어그램
   - 메시지 프로토콜 명세 (queue.proto 참조)
   - Redis 키 설계
   - 시퀀스 다이어그램 (접속 → 매칭 → 입장 → 게임 → 종료)
   - 비정상 종료 복구 흐름
   - 부하 테스트 결과 요약
3. README.md 작성:
   - 빌드 방법 (CMake + vcpkg)
   - 실행 방법 (Redis + 큐 서버)
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
| `test_match_queue.cpp` | 유닛 | Step 4 | 매칭 진입/퇴장/순번/성립/취소/중복 |
| `test_token_generator.cpp` | 유닛 | Step 5 | UUID 포맷, 유일성 |
| `test_codec.cpp` | 유닛 | Step 3 | Envelope oneof 직렬화 라운드트립, 에러 입력 |
| `test_config.cpp` | 유닛 | Step 9 | JSON 파싱, 기본값, 누락 필드 |
| `test_ratelimit_logic.cpp` | 유닛 | Step 6 | 카운터 로직 (Redis mock) |
| `test_client_connect.cpp` | 피처 | Step 2~3 | TCP 접속/해제/하트비트 타임아웃/Envelope 송수신 |
| `test_match_flow.cpp` | 피처 | Step 4 | 2클라이언트 매칭 E2E, 팀 배정, 이탈 처리 |
| `test_token_validate.cpp` | 피처 | Step 5 | Redis 토큰 TTL, 일회용, 만료 |
| `test_ratelimit_redis.cpp` | 피처 | Step 6 | Redis Rate Limit 실동작, IP별 독립 |
| `test_internal_channel.cpp` | 피처 | Step 7 | 토큰 검증, SlotReleased, 하트비트, TTL 만료 |
| `test_edge_cases.cpp` | 피처 | Step 7 | 동시 취소, 중복 요청, 비정상 종료 복구 |

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
[Step 1] 프로젝트 스캐폴딩 + queue.proto 확정
    │
    ▼
[Step 2] TCP 서버 골격 ──────────────┐
    │                                │
    ▼                                ▼
[Step 3] Protobuf Envelope 통합  (Redis 설치/Docker)
    │                                │
    ▼                                │
[Step 4] 매칭 대기열 로직 ◄─────────┘
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
| **실행 바이너리** | `queue-server.exe` (Windows) |
| **설정 파일** | `server_config.json` |
| **Protobuf 스키마** | `queue.proto` (C++/C# 공유, 별도 저장소) |
| **테스트 바이너리** | `unit-tests.exe`, `feature-tests.exe` (Catch2) |
| **부하 테스트** | `load_client.py` + `docs/benchmark.md` |
| **문서** | `README.md`, `docs/architecture.md` |
| **Docker Compose** | Redis + 큐 서버 일괄 실행 (선택) |
