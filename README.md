# Project-SOS-Backend

Project-SOS (멀티플레이어 RTS 게임)의 백엔드 인프라 및 서버 프로젝트.

## 아키텍처

```
[Unity Game Server :7979]
    │ 로컬 파일 로그
    ▼
[Vector] ──────────────────► [ClickHouse] ──► [Grafana :3000]
    ▲                          로그 저장소       대시보드/시각화
    │ Docker stdout
[Room Server :8080/:8081]
[Chat Server :8082/:8083]
    │
    ▼
[Redis :6379]
  세션, 토큰, 캐시
```

| 서비스 | 기술 스택 | 역할 |
|--------|----------|------|
| Room Server | C++ / Boost.Asio / Protobuf | 방 관리, 게임 시작, 토큰 발급 |
| Chat Server | C++ / Boost.Asio / Protobuf | 인게임 채팅 (로비/전체/귓속말) |
| Logging Pipeline | Vector → ClickHouse → Grafana | 전 서비스 통합 로그 수집/저장/시각화 |
| Redis | redis:7-alpine | 공통 운영 저장소 |

## 기술 스택

| 항목 | 선택 |
|------|------|
| C++ Standard | C++20 |
| Build System | CMake 3.20+ |
| 비동기 I/O | Boost.Asio |
| 직렬화 | Protobuf |
| 인프라 | Docker Compose |
| 로그 수집 | Vector 0.41.1 |
| 로그 저장 | ClickHouse 24.3 |
| 대시보드 | Grafana 11.4.0 |

## 프로젝트 구조

```
Project-SOS-Backend/
├── proto/                        # Protobuf 정의 (room.proto, chat.proto)
├── src/
│   ├── common/                   # 공유 라이브러리 (protocol, redis, ratelimit, util)
│   ├── room/                     # Room Server
│   └── chat/                     # Chat Server
├── tests/                        # Catch2 유닛 테스트
├── scripts/                      # Python E2E/부하 테스트
├── infra/
│   ├── clickhouse/
│   │   ├── init.sql              # DB + 테이블 + MV 생성
│   │   └── users.xml             # ClickHouse 서버 설정
│   ├── vector/
│   │   ├── vector.toml           # 로그 수집 파이프라인
│   │   └── vector-tests.toml     # VRL 파싱 단위 테스트
│   └── grafana/
│       ├── datasources.yml       # ClickHouse 데이터소스
│       ├── dashboards.yml        # 대시보드 프로비저닝
│       ├── dashboards/           # JSON 대시보드
│       └── provisioning/alerting/  # 알림 규칙 프로비저닝
├── config/
│   └── server_config.json        # 서버 설정 기본값 템플릿
├── docs/                         # 시스템 설계 및 계획 문서
├── Dockerfile                    # 멀티 스테이지 멀티 타겟 (room-server / chat-server)
├── docker-compose.yml
├── CMakeLists.txt
├── vcpkg.json                    # vcpkg 의존성 매니페스트
└── .env.example                  # 환경변수 템플릿
```

## 시작하기

### 사전 요구사항

- Docker Desktop
- Git

### 환경변수 설정

```bash
cp .env.example .env
```

`.env` 파일에서 `GAME_LOG_PATH`를 Unity 로그 경로로 설정:

```
# Windows 예시
GAME_LOG_PATH=/c/Users/{user}/AppData/LocalLow/{Company}/{Product}/Logs
```

### 인프라 실행

```bash
docker compose up -d
```

6개 서비스가 기동된다:

| 서비스 | 접속 |
|--------|------|
| Room Server | localhost:8080 (클라이언트), localhost:8081 (내부) |
| Chat Server | localhost:8082 (클라이언트), localhost:8083 (내부) |
| Redis | localhost:6379 |
| ClickHouse | localhost:8123 (HTTP), localhost:9000 (Native) |
| Vector | (내부 전용, 포트 없음) |
| Grafana | http://localhost:3000 (admin/admin) |

### 상태 확인

```bash
docker compose ps
```

ClickHouse와 Redis는 healthcheck가 설정되어 있으며, Vector와 Grafana는 ClickHouse healthy 상태를 확인한 후 기동된다.

### C++ 빌드 (로컬 개발)

Developer Command Prompt for VS 2022에서 실행:

```bash
cmake --preset=default          # Configure (vcpkg 자동 설치)
cmake --build build             # Build
build\src\room\room-server.exe  # Room Server 실행
build\src\chat\chat-server.exe  # Chat Server 실행
```

### 테스트

**Catch2 유닛 테스트:**

```bash
cmake --build build --target unit-tests
cd build && ctest --output-on-failure
```

**Python E2E 테스트** (서버 실행 상태 필요):

```bash
python scripts/test_room.py     # Room Server E2E (13 시나리오)
python scripts/test_chat.py     # Chat Server E2E
python scripts/test_load.py     # 부하 테스트
```

**Vector 파싱 테스트:**

```bash
docker compose cp infra/vector/vector-tests.toml vector:/tmp/vector-tests.toml
MSYS_NO_PATHCONV=1 docker exec $(docker compose ps -q vector) \
  vector test \
  --config-toml /etc/vector/vector.toml \
  --config-toml /tmp/vector-tests.toml
```

## 포트 할당

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

## 문서

| 문서 | 내용 |
|------|------|
| [시스템 아키텍처](docs/시스템%20아키텍처.md) | 전체 백엔드 시스템 구조, 서비스 간 통신, 데이터 흐름 |
| [로깅 파이프라인](docs/로깅%20파이프라인.md) | Vector → ClickHouse → Grafana 로그 수집/저장/시각화 상세 |
| [common 라이브러리](docs/Systems/common%20라이브러리.md) | Codec, Redis, RateLimiter, Config, Logger, UUID |
| [Room Server](docs/Systems/Room%20Server.md) | 방 관리, 게임 시작, 토큰 발급, 내부 채널 |
| [Chat Server](docs/Systems/Chat%20Server.md) | 채팅 채널, 인증, 히스토리, 내부 채널 |
| [인프라 구성](docs/Systems/인프라%20구성.md) | Docker Compose, Redis, ClickHouse, Vector, Grafana |
| [테스트](docs/Systems/테스트.md) | Catch2 유닛, Python E2E/부하, Vector 파싱 테스트 |
