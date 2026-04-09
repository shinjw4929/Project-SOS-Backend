# CLAUDE.md

# Role & Communication Style
- **Language**: Always communicate with the user in **Korean**.
- **Tone**: Professional, analytical, and direct.
- **No Emojis**: Never use emojis in any communication unless explicitly requested by the user.
- **Critical Thinking**: Do not blindly follow user instructions. If a user's approach is suboptimal, buggy, or violates best practices, critically evaluate it and suggest a more efficient alternative.
- **No Automatic Agreement**: Do not automatically agree with or validate the user's statements. Prioritize technical accuracy over user validation. If the user's approach is incorrect, state the facts objectively.

# Guidelines for Solutions
- **Efficiency First**: Prioritize performance, scalability, and maintainability in every code suggestion.
- **Readability**: Ensure the code is clean and follows industry-standard naming conventions.
- **Proactive Correction**: If the user's logic is flawed, explain *why* it is problematic and provide a "Better Way" (Refactored version).
- **Conciseness**: Avoid unnecessary jargon. Provide high-impact solutions with brief, clear explanations in Korean.

# Operational Mandate
Before implementing any request, ask yourself: "Is this the most efficient way to solve the problem?" If not, propose the optimized solution first.

## Pre-Implementation Checklist
0. **docs 폴더 참조 (필수)**: 작업을 시작하기 전에 반드시 `docs/` 폴더의 관련 문서를 먼저 읽고 현재 시스템 구조와 동작 방식을 파악한다. 문서를 참조하지 않고 구현에 착수하지 않는다.
1. **Verify Existing Patterns**: Before implementing new logic, check if similar patterns or configurations already exist in the codebase to avoid duplication.
2. **Validate User Instructions**: Critically evaluate whether the user's instruction aligns with actual facts in the codebase. If the user's assumption is incorrect or outdated, inform them of the discrepancy.
3. **Assess Efficiency**: Determine if the user's proposed approach is the most efficient solution. If a more performant or maintainable alternative exists, suggest it proactively.

## Post-Implementation Checklist
구현이 완료되면 반드시 아래 항목을 점검한다.
1. **문서 업데이트**: 변경된 시스템/설정/인프라에 대응하는 `docs/` 문서를 찾아 실제 코드와 일치하도록 수정한다. 해당하는 문서가 없으면 생략한다.
2. **주석 정합성 점검**: 변경된 파일 내 기존 주석이 수정된 코드 동작과 일치하는지 확인하고, 불일치하는 주석을 수정 또는 제거한다. 새 주석은 로직이 자명하지 않은 경우에만 추가한다.
3. **CLAUDE.md 동기화**: 주요 패턴, 시스템 플로우, 포트 할당 등 CLAUDE.md에 기재된 내용이 변경되었다면 함께 업데이트한다.

---

## Project Overview

Project-SOS-Backend는 Project-SOS (멀티플레이어 RTS 게임)의 백엔드 인프라 및 서버 프로젝트다.

- **Room Server**: C++ / Boost.Asio / TCP + Protobuf (방 관리/게임 시작)
- **Chat Server**: C++ / Boost.Asio / TCP + Protobuf (인게임 채팅)
- **Logging Pipeline**: Vector → ClickHouse → Grafana (전 서비스 통합 로그)
- **공통 운영 저장소**: Redis (세션, 토큰, Rate Limit)

### Redis 키 구조

| 키 패턴 | 타입 | TTL | 용도 |
|---------|------|-----|------|
| `token:{uuid}` | STRING (JSON) | 60초 | 입장 토큰 (`{player_id, session_id}`) |
| `active_sessions` | SET | - | 진행 중인 게임 세션 ID |
| `game_server:{server_id}` | STRING | 90초 | 게임 서버 하트비트 |
| `rate:{ip}` | STRING | 10초 | Room Rate Limit 카운터 |
| `chat:rate:{player_id}` | STRING | 5초 | Chat Rate Limit 카운터 |
| `chat:session:{session_id}` | HASH | 7200초 | 게임 세션 플레이어 매핑 (player_id -> player_name) |
| `chat:player:{player_id}` | STRING | 7200초 | 플레이어-세션 매핑 |
| `chat:name:{player_id}` | STRING | 7200초 | 플레이어 표시명 |
| `chat:history:{session_id}:ALL` | LIST | 7200초 | 세션 채팅 히스토리 (최근 20개) |

## Build & Development

- **C++ Standard**: C++20
- **Compiler**: MSVC 19.44 (Visual Studio 2022 Community)
- **Build System**: CMake 4.2 + Ninja + vcpkg
- **Package Manager**: vcpkg (`VCPKG_ROOT=C:\vcpkg`)
- **IDE**: CLion (Visual Studio 툴체인 + Ninja)
- **Infrastructure**: Docker Compose
- **Game Server Port**: 7979 (Unity Netcode for Entities)

### 빌드 방법 (CLI)

```bash
# Developer Command Prompt for VS 2022에서 실행
cmake --preset=default          # Configure (vcpkg 자동 설치)
cmake --build build             # Build
build\src\room\room-server.exe  # 실행
```

### CMake 타겟

| 타겟 | 종류 | 설명 |
|------|------|------|
| `sos_common` | STATIC 라이브러리 | Protobuf 코드젠 + 공유 코드 |
| `room-server` | 실행 파일 | Room Server |
| `chat-server` | 실행 파일 | Chat Server |
| `unit-tests` | 실행 파일 | Catch2 유닛 테스트 (Codec, UUID, Config, Room) |

### 테스트 실행

```bash
# Developer Command Prompt for VS 2022에서 실행
cmake --build build --target unit-tests
cd build && ctest --output-on-failure
```

---

## Repository Structure

```
Project-SOS-Backend/
├── proto/                    # Protobuf 정의 (room.proto, chat.proto)
├── src/
│   ├── common/               # 공유 라이브러리 (protocol, redis, ratelimit, util)
│   ├── room/                 # Room Server (방 관리/게임 시작)
│   │   ├── server/           # RoomServer (TCP acceptor), ClientSession
│   │   ├── room/             # Room, RoomManager
│   │   └── internal/         # GameServerChannel (:8081), GameServerSession, ChatServerChannel
│   └── chat/                 # Chat Server (채팅)
│       ├── server/           # ChatServer (TCP acceptor), ChatSession
│       ├── channel/          # ChannelManager (로비/세션 채널)
│       └── internal/         # InternalChannel (:8083), InternalSession
├── infra/
│   ├── clickhouse/
│   │   ├── init.sql          # DB + 테이블 + MV 생성
│   │   └── users.xml         # ClickHouse 서버 설정
│   ├── vector/
│   │   └── vector.toml       # 로그 수집 파이프라인
│   └── grafana/
│       ├── datasources.yml   # ClickHouse 데이터소스
│       ├── dashboards.yml    # 대시보드 프로비저닝
│       ├── dashboards/       # JSON 대시보드 파일
│       └── provisioning/alerting/  # 알림 규칙 프로비저닝
├── tests/                    # Catch2 유닛 테스트
├── scripts/                  # Python E2E/부하 테스트
├── docs/
│   ├── Documentation-Checklist.md  # 문서 관리 규칙 + 매핑 테이블
│   ├── Checklists/            # 스킬 공유 참조 (패턴 탐색, 코드 리뷰)
│   ├── Systems/               # 모듈별 상세 문서 (common, Room, Chat, 인프라)
│   ├── Plans/                 # 구현 계획 + Completed/ 아카이브
│   ├── Internal/Analysis/     # 분석 문서
│   └── WorkLog/               # 날짜별 구현 기록
├── config/
│   └── server_config.json    # 서버 설정 기본값 템플릿
├── Dockerfile                # 멀티 스테이지 멀티 타겟 (build-base -> room-server / chat-server)
├── .dockerignore
├── docker-compose.yml
├── CMakeLists.txt            # 루트 빌드 설정
├── CMakePresets.json         # CMake 프리셋 (vcpkg 통합)
├── vcpkg.json                # vcpkg 의존성 매니페스트
├── .env                      # 환경변수 (gitignore)
└── .env.example              # 환경변수 템플릿
```

---

## Infrastructure

### Docker Compose Services

| 서비스 | 이미지 / 빌드 | 포트 | 역할 |
|--------|--------------|------|------|
| Redis | `redis:7-alpine` | 6379 | Room/Chat 운영 저장소 |
| ClickHouse | `clickhouse/clickhouse-server:24.3` | 8123 (HTTP), 9000 (Native) | 로그 저장소 |
| Vector | `timberio/vector:0.41.1-debian` | - | 로그 수집 + 파싱 + 전송 |
| Grafana | `grafana/grafana:11.4.0` | 3000 | 대시보드 + 시각화 |
| room-server | `Dockerfile` target: room-server | 8080, 8081 | Room Server (C++) |
| chat-server | `Dockerfile` target: chat-server | 8082, 8083 | Chat Server (C++) |

### Port Assignment

| 포트 | 서비스 | 방향 |
|------|--------|------|
| 7979 | Game Server (Unity) | Client-facing |
| 8080 | Room Server | Client-facing |
| 8081 | Room Server | 내부 (Game Server 연결) |
| 8082 | Chat Server | Client-facing |
| 8083 | Chat Server | 내부 (Room/Game Server 연결) |
| 6379 | Redis | 내부 |
| 8123 | ClickHouse HTTP | 내부 |
| 9000 | ClickHouse Native | 내부 |
| 3000 | Grafana | Admin-facing |

---

## Logging Pipeline

### Data Flow

```
[Unity Game Server] → 로컬 파일 → [Vector] file source ──→ [ClickHouse service_events] → [Grafana]
[Room Server (C++)] → Docker stdout → [Vector] docker_logs ─┘
[Chat Server (C++)] → Docker stdout → [Vector] docker_logs ──┤
                                                              └─ [Chat:Message] → route_cpp → [ClickHouse chat_messages] → [Grafana]
```

### Log Format (전 서비스 공통)

```
<timestamp> | <LEVEL> | [<World/Category>] <message>, key=value, ...
```

Game Server 예시:
```
2026-03-16 12:34:43.357 | DEBUG   | [S:Wave] Wave0 spawned, count=5
2026-03-16 12:34:43.415 | INFO    | [S:Network] Client connected, networkId=1
```

C++ Server 예시:
```
2026-03-16 10:00:01.100 | INFO    | [Room] Room started, room_id=abc, players=4
2026-03-16 10:01:01.100 | INFO    | [Chat] Message sent, channel=ALL
```

### ClickHouse Table: `project_sos.service_events`

| 컬럼 | 타입 | 용도 |
|------|------|------|
| timestamp | DateTime64(3, 'UTC') | 이벤트 시각 |
| service | LowCardinality(String) | game / room / chat |
| level | LowCardinality(String) | DEBUG / INFO / WARNING / ERROR |
| category | LowCardinality(String) | Combat / Wave / Economy / Match / Chat 등 |
| message | String | 로그 메시지 본문 |
| world | LowCardinality(String) | S / C (Game Server 전용) |
| network_id / ghost_id | Int32 | Game Server 전용 |

### ClickHouse Table: `project_sos.chat_messages`

| 컬럼 | 타입 | 용도 |
|------|------|------|
| timestamp | DateTime64(3, 'UTC') | 메시지 시각 |
| session_id | String | 게임 세션 ID (LOBBY는 빈값) |
| channel | LowCardinality(String) | LOBBY / ALL / WHISPER |
| sender_id | String | 발신자 ID |
| sender_name | String | 발신자 표시명 |
| target_id | String | 귓속말 대상 (WHISPER 전용) |
| content | String | 메시지 내용 |

### Grafana Dashboards

| 대시보드 | 내용 |
|---------|------|
| Service Overview | 로그 볼륨, 레벨/카테고리 분포, Warning/Error 테이블 |
| Game Events | Wave 전환, 킬 카운트, 경제 이벤트, 적 스폰, 이동 경고, Hero 사망 |
| Room Metrics | 방 생성/제거, 플레이어 입퇴장, 게임 시작, 토큰/세션, 내부 채널, 에러 |
| Chat Metrics | 접속 추이, 인증, 세션 생성/종료, 채널별 메시지, 메시지 로그, Rate Limit, 내부 채널, 에러 |

---

## C++ Server Architecture

### Module Structure

```
src/
├── common/          # STATIC 라이브러리 (공유)
│   ├── protocol/    # Protobuf 직렬화/역직렬화
│   ├── redis/       # Redis 클라이언트 래퍼, SessionStore (토큰/세션 관리)
│   ├── ratelimit/   # Rate limiting
│   └── util/        # 공통 유틸리티
├── room/            # Room Server 실행 파일
└── chat/            # Chat Server 실행 파일
```

### Chat Channels

| 채널 | 범위 | 히스토리 |
|------|------|---------|
| LOBBY | 전체 접속자 | 없음 |
| ALL | 같은 세션 전체 | Redis TTL 2시간, 최근 20개 |
| WHISPER | 1:1 | 없음 |

---

## Development Guidelines

### 기본 원칙
1. **C++20**: smart pointer, std::optional, std::span, concepts 등 모던 기능 적극 활용
2. **RAII**: 리소스 관리는 RAII 패턴 필수. raw pointer 사용 금지 (외부 라이브러리 인터페이스 제외)
3. **스레드 안전**: Boost.Asio strand 기반 동시성 관리. 공유 자원 접근 시 mutex/lock 필수
4. **네이밍**: 변수명은 의미를 알 수 있도록 작성하며 축약하지 않는다 (예: `auto c` 금지 → `auto connection` 사용)
5. **에러 처리**: 네트워크 I/O 에러는 반드시 처리. 예외보다 에러 코드 선호 (Boost.Asio 패턴)

### 인프라 규칙
1. **환경변수**: 비밀번호, 경로 등은 `.env`로 관리. 하드코딩 금지
2. **Docker**: 서비스 추가 시 `docker-compose.yml` 업데이트 + 포트 충돌 확인
3. **로그 형식**: Vector 파싱 정규식과 일치하는 형식으로 출력. 형식 변경 시 `vector.toml`도 함께 수정
4. **ClickHouse 스키마**: 테이블 변경 시 `init.sql` 업데이트 + 기존 데이터 호환성 확인

### 네트워크 규칙
1. **Protobuf**: 메시지 정의는 `proto/` 폴더에서 관리. 변경 시 양쪽(C++ / Unity) 재생성
2. **Rate Limiting**: 클라이언트 입력에 대한 속도 제한 필수
3. **인증**: 토큰/세션 검증 필수. 미인증 요청은 즉시 거부
4. **연결 관리**: 타임아웃, graceful shutdown, 재연결 처리 필수

---

## Documentation

### 문서 관리

문서 폴더 구조, 변경 유형별 업데이트 대상, 작성 원칙은 `docs/Documentation-Checklist.md`를 참조한다.

### 커밋 메시지 작성 가이드

```
<제목: 작업 내용 요약 (한 줄)>

<세부 작업 내용>
- 변경된 파일/시스템 목록
- 수정 의도 및 해결한 문제
- 주요 변경 사항
```

**작성 원칙**:
1. **제목**: 무엇을 했는지 명확하게 요약
2. **본문**: 제목과 빈 줄 하나 띄우고 세부 내용 작성
3. **의도 명시**: 단순 변경 사항 나열이 아닌, 왜 이 변경이 필요했는지 드러나도록 작성
4. **간결함**: 불필요한 설명 없이 핵심만 기술
5. **Co-Authored-By 금지**: 커밋 메시지에 `Co-Authored-By` 트레일러를 절대 추가하지 않는다
