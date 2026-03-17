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
├── proto/                        # Protobuf 정의
├── src/
│   ├── common/                   # 공유 라이브러리 (protocol, redis, ratelimit, util)
│   ├── room/                     # Room Server
│   └── chat/                     # Chat Server
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
│       └── dashboards/           # JSON 대시보드
├── docs/                         # 시스템 설계 및 계획 문서
├── docker-compose.yml
├── CMakeLists.txt
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

4개 서비스가 기동된다:

| 서비스 | 접속 |
|--------|------|
| Grafana | http://localhost:3000 (admin/admin) |
| ClickHouse HTTP | http://localhost:8123 |
| ClickHouse Native | localhost:9000 |
| Redis | localhost:6379 |

### 상태 확인

```bash
docker compose ps
```

ClickHouse와 Redis는 healthcheck가 설정되어 있으며, Vector와 Grafana는 ClickHouse healthy 상태를 확인한 후 기동된다.

### Vector 테스트 실행

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
| [룸 서버 구축 계획](docs/계획/룸%20서버/C++%20룸%20서버%20구축%20계획.md) | Room Server 설계 및 구현 계획 |
| [채팅 서버 구축 계획](docs/계획/채팅%20서버/채팅%20서버%20구축%20계획.md) | Chat Server 설계 및 구현 계획 |
| [로깅 시스템 구축 계획](docs/계획/로깅/로깅%20시스템%20구축%20계획.md) | 로깅 파이프라인 전체 계획 (Tier 1~2) |
