# 인프라 리뷰 및 Vector 테스트 추가 (2026-03-16)

## 개요

코드 리뷰를 통해 로깅 파이프라인 인프라의 보안/안정성 문제 4건을 수정하고, 성능 최적화 2건을 적용했다. 이후 Vector VRL 파싱 로직에 대한 단위 테스트 7개를 추가했다.

---

## 인프라 수정 사항

### 1. Grafana 데이터소스 비밀번호 환경변수화

**문제**: `datasources.yml`에 ClickHouse 비밀번호가 `dev123`으로 하드코딩되어 있어, `.env`에서 `CH_PASSWORD`를 변경하면 Grafana 연결이 끊어짐.

**수정**:
- `datasources.yml`: `password: dev123` → `password: $CH_PASSWORD`
- `docker-compose.yml`: Grafana 서비스에 `CH_PASSWORD` 환경변수 전달 추가

### 2. Vector 체크포인트 영속화

**문제**: Vector에 data 볼륨이 없어서 컨테이너 재시작 시 file source 체크포인트가 유실됨. game_logs가 매번 처음부터 재수집되어 ClickHouse에 중복 데이터 적재.

**수정**:
- `docker-compose.yml`: `vector_data:/var/lib/vector` 볼륨 추가
- `volumes` 섹션에 `vector_data` 선언

### 3. ClickHouse/Redis healthcheck 및 서비스 의존성

**문제**: `depends_on`에 healthcheck 조건이 없어서 ClickHouse 초기화(init.sql) 완료 전에 Vector/Grafana가 연결을 시도할 수 있음.

**수정**:
- ClickHouse: `healthcheck` 추가 (`clickhouse-client -q 'SELECT 1'`, interval 5s, retries 10, start_period 10s)
- Redis: `healthcheck` 추가 (`redis-cli -a <password> ping`, interval 5s, retries 5)
- Vector/Grafana: `depends_on.clickhouse.condition: service_healthy`

### 4. Redis 인증 설정

**문제**: Redis가 인증 없이 노출되어 있음.

**수정**:
- `docker-compose.yml`: `command: redis-server --requirepass ${REDIS_PASSWORD:-devredis}`
- `.env.example`: `REDIS_PASSWORD=devredis` 항목 추가

---

## 성능 최적화

### Vector → ClickHouse 배치/버퍼

**문제**: sink에 배치/버퍼 설정이 없어서 기본값(500이벤트/1초)으로 동작. 전투/웨이브 버스트 구간에서 소량 insert가 빈번하게 발생하여 ClickHouse I/O 부하 발생.

**수정** (`vector.toml`):
```toml
batch.max_events = 1000
batch.timeout_secs = 5

[sinks.clickhouse.buffer]
type = "disk"
max_size = 536870912   # 512 MB
when_full = "block"
```

- 이벤트를 최대 1000개 또는 5초 단위로 모아서 전송
- 디스크 버퍼로 Vector 비정상 종료 시에도 이벤트 유실 방지

### Grafana 대시보드 새로고침 주기

**문제**: 10초마다 16개 쿼리가 ClickHouse에 발생 (Service Overview 9패널 + Game Events 7패널).

**수정**: `refresh: "10s"` → `refresh: "30s"` (두 대시보드 모두)

---

## Vector 단위 테스트 추가

### 파일 구조

테스트는 파이프라인 설정(`vector.toml`)과 분리하여 `vector-tests.toml`에 관리한다. Vector의 `--config-toml` 다중 지정으로 두 파일을 병합하여 실행한다.

```
infra/vector/
├── vector.toml          # 파이프라인 설정 (sources, transforms, sinks)
└── vector-tests.toml    # 단위 테스트 (tests)
```

### 실행 방법

```bash
docker compose cp infra/vector/vector.toml vector:/tmp/vector.toml
docker compose cp infra/vector/vector-tests.toml vector:/tmp/vector-tests.toml
docker exec <vector-container> vector test \
  --config-toml /tmp/vector.toml \
  --config-toml /tmp/vector-tests.toml
```

### 테스트 목록 (7개)

| 테스트명 | 주입 지점 | 검증 내용 |
|---------|----------|-----------|
| `game_info_with_network_id` | parse_game → clean_fields | INFO 레벨 파싱, networkId 추출 |
| `game_debug_with_ghost_id` | parse_game → clean_fields | DEBUG 레벨 파싱, idx(ghost_id) 추출 |
| `game_client_side_log` | parse_game → clean_fields | Client-side(C) world 파싱, ERROR 레벨 |
| `game_no_kvp_fields` | parse_game → clean_fields | key=value 없는 로그에서 기본값 0 할당 |
| `game_invalid_format_dropped` | parse_game | 잘못된 형식 → abort로 드롭 확인 |
| `cpp_queue_fields_pass_through` | clean_fields | Queue 필드 통과 + Docker 메타데이터 제거 |
| `cpp_chat_fields_pass_through` | clean_fields | Chat 필드 통과 + label 제거 |

### 테스트 구조 설명

- **Game Server (5개)**: `parse_game` → `clean_fields` 전체 파이프라인 통과. 정규식 파싱, 필드 추출, 드롭 로직을 검증
- **C++ Server (2개)**: `clean_fields` 직접 주입. Vector test framework의 `log_fields`가 flat 값만 지원하여 Docker label 중첩 객체(`.label."vector.service"`)를 주입할 수 없기 때문에, `parse_cpp` end-to-end 테스트 대신 `clean_fields`에서 필드 구조와 메타데이터 제거를 검증

### 검증된 파싱 시나리오

```
Game Server 로그:
  <timestamp> | <LEVEL> | [<S/C>:<Category>] <message>, networkId=N, idx=N
  → service, level, world, category, message, network_id, ghost_id

C++ Server 로그:
  <timestamp> | <LEVEL> | [<Category>] <message>, depth=N, wait_ms=N, channel=X, rate_limit=N
  → service, level, category, message + 서비스별 전용 필드
```

---

## 변경 파일 목록

| 파일 | 변경 내용 |
|------|-----------|
| `docker-compose.yml` | Redis 인증, healthcheck, depends_on condition, Vector 볼륨, Grafana 환경변수 |
| `infra/vector/vector.toml` | 배치/버퍼 설정 추가 (테스트는 별도 파일로 분리) |
| `infra/vector/vector-tests.toml` | VRL 파싱 단위 테스트 7개 (신규) |
| `infra/grafana/datasources.yml` | 비밀번호 환경변수화 |
| `infra/grafana/dashboards/service-overview.json` | refresh 10s → 30s |
| `infra/grafana/dashboards/game-events.json` | refresh 10s → 30s |
| `.env.example` | `REDIS_PASSWORD` 항목 추가 |
