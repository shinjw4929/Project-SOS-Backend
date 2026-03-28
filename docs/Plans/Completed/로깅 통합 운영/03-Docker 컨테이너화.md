# Step 5-2: Docker 컨테이너화

## Context

Room/Chat 서버를 Docker 이미지로 빌드하여 Compose 스택에 통합한다. 현재 빌드는 MSVC/Windows 전용이며, Docker에서는 Linux(GCC) 빌드가 필요하다.

---

## 설계 결정

| 항목 | 결정 | 근거 |
|------|------|------|
| Dockerfile 수 | 1개 (프로젝트 루트) | Room/Chat이 common/proto 공유, 멀티 타겟으로 분리 |
| 빌드 이미지 | `ubuntu:24.04` | GCC 14 기본 포함, C++20 완전 지원, vcpkg 호환 |
| 런타임 이미지 | `ubuntu:24.04` minimal | libstdc++6만 설치 |
| 빌드 컨텍스트 | 프로젝트 루트 (`.`) | src/common/, proto/, CMakeLists.txt 등 필요 |
| 설정 주입 | 환경변수 fallback | Docker 표준 패턴, Config.cpp에 3줄 추가 |

### 빌드 컨텍스트 문제

기존 docker-compose.yml에 `build: ./src/room`으로 정의되어 있으나, 빌드에 프로젝트 루트의 `CMakeLists.txt`, `proto/`, `src/common/`, `vcpkg.json` 등이 필요하다. `context: .` + `dockerfile: Dockerfile` + `target:` 구조로 변경.

---

## 수정

### [ ] `Dockerfile` 생성 (프로젝트 루트)

멀티 스테이지 멀티 타겟:

```dockerfile
# === Build stage (공유) ===
FROM ubuntu:24.04 AS build-base
RUN apt-get update && apt-get install -y \
    build-essential g++ cmake ninja-build git curl zip unzip tar pkg-config \
    linux-headers-generic
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git $VCPKG_ROOT && \
    $VCPKG_ROOT/bootstrap-vcpkg.sh

WORKDIR /app
COPY vcpkg.json ./
RUN $VCPKG_ROOT/vcpkg install --triplet x64-linux
COPY CMakeLists.txt CMakePresets.json ./
COPY proto/ proto/
COPY src/ src/
RUN cmake --preset=default && cmake --build build

# === Room Server ===
FROM ubuntu:24.04 AS room-server
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build-base /app/build/src/room/room-server /usr/local/bin/
COPY config/ /app/config/
WORKDIR /app
CMD ["room-server", "config/server_config.json"]

# === Chat Server ===
FROM ubuntu:24.04 AS chat-server
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build-base /app/build/src/chat/chat-server /usr/local/bin/
COPY config/ /app/config/
WORKDIR /app
CMD ["chat-server", "config/server_config.json"]
```

`CMD`는 exec form 사용 — PID 1에서 SIGTERM 수신을 위해 필수.

### [ ] `.dockerignore` 생성

```
build/
cmake-build-*/
.git/
.env
.idea/
.vscode/
docs/
infra/
tests/
scripts/
node_modules/
vcpkg_installed/
*.md
```

### [ ] `docker-compose.yml` 수정

주석 처리된 서버 서비스(line 68-80)를 교체:

```yaml
room-server:
  build:
    context: .
    dockerfile: Dockerfile
    target: room-server
  ports: ["8080:8080", "8081:8081"]
  depends_on:
    redis: { condition: service_healthy }
    chat-server: { condition: service_started }
  environment:
    REDIS_HOST: redis
    REDIS_PORT: 6379
    REDIS_PASSWORD: ${REDIS_PASSWORD:-devredis}
    CHAT_SERVER_HOST: chat-server
    CHAT_SERVER_PORT: 8083
  labels:
    vector.service: "room"
  restart: unless-stopped

chat-server:
  build:
    context: .
    dockerfile: Dockerfile
    target: chat-server
  ports: ["8082:8082", "8083:8083"]
  depends_on:
    redis: { condition: service_healthy }
  environment:
    REDIS_HOST: redis
    REDIS_PORT: 6379
    REDIS_PASSWORD: ${REDIS_PASSWORD:-devredis}
  labels:
    vector.service: "chat"
  restart: unless-stopped
```

### [ ] `src/common/util/Config.cpp` 환경변수 fallback 추가

생성자 끝에 5줄 추가:

```cpp
if (auto* val = std::getenv("REDIS_HOST")) data_["redis_host"] = val;
if (auto* val = std::getenv("REDIS_PORT")) data_["redis_port"] = std::stoi(val);
if (auto* val = std::getenv("REDIS_PASSWORD")) data_["redis_password"] = val;
if (auto* val = std::getenv("CHAT_SERVER_HOST")) data_["chat_server_host"] = val;
if (auto* val = std::getenv("CHAT_SERVER_PORT")) data_["chat_server_port"] = std::stoi(val);
```

> Room Server가 Chat Server에 SessionCreated/Ended를 전송하므로, Docker 환경에서 `chat-server` (Docker DNS)로 연결해야 한다.

### [ ] `CMakeLists.txt` (루트) tests/ 조건부 빌드

```cmake
# 기존:
enable_testing()
add_subdirectory(tests)

# 변경:
if(EXISTS "${CMAKE_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()
```

> `.dockerignore`에서 `tests/`를 제외하므로 Docker 빌드 시 tests/ 디렉토리가 없다. CMake configure 실패를 방지.

### [ ] `.env.example` 업데이트

`REDIS_HOST`, `REDIS_PORT` 항목 추가.

---

## 위험 요소

| 위험 | 대응 |
|------|------|
| vcpkg Linux 빌드 실패 (libssl-dev 등) | Dockerfile apt-get에 추가 의존성 설치 |
| 첫 빌드 10-20분 | BuildKit 캐시: vcpkg.json을 소스보다 먼저 COPY하여 의존성 레이어 캐시 |

---

## 검증

- `docker compose up --build` 성공
- Room/Chat 서버 컨테이너가 Redis에 정상 연결
- 컨테이너 stdout에서 올바른 로그 형식 출력
- Vector가 `vector.service` 라벨로 로그 수집
