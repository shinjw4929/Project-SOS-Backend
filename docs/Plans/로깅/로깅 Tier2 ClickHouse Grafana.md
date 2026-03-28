# 로깅 Tier 2: ClickHouse + Grafana (전 서비스 통합)

> **구현 완료**. 실제 구현 상태는 [로깅 파이프라인](../../로깅%20파이프라인.md)을 참조.
> 이 문서는 초기 설계 계획을 기록한다. 구현 과정에서 `clean_fields` transform 추가, 배치/버퍼 설정, healthcheck 등이 변경되었다.

## 목적

Game Server, Room Server, Chat Server 3개 서비스의 로그를 단일 ClickHouse 인스턴스에 수집하여 포렌식 조회, 집계/추이 분석, 실시간 대시보드, 알림을 제공한다.

---

## ClickHouse 테이블 스키마

### 메인 이벤트 테이블

```sql
CREATE DATABASE IF NOT EXISTS project_sos;

CREATE TABLE project_sos.service_events (
    -- 공통 필드
    timestamp DateTime64(3, 'UTC'),
    service LowCardinality(String),           -- game / room / chat
    build_version LowCardinality(String),
    session_id String DEFAULT '',
    server_id LowCardinality(String) DEFAULT 'dev',
    level LowCardinality(String),             -- DEBUG / INFO / WARNING / ERROR
    category LowCardinality(String),          -- Combat / Wave / Economy / Network / Movement / System / Match / Chat
    record_type LowCardinality(String) DEFAULT 'log',  -- log / violation / error
    message String,

    -- Game Server 전용
    world LowCardinality(String) DEFAULT '',  -- S / C
    violation_type LowCardinality(String) DEFAULT '',
    entity_type LowCardinality(String) DEFAULT '',
    ghost_id Int32 DEFAULT 0,
    team_id Int8 DEFAULT 0,
    network_id Int32 DEFAULT 0,

    -- 유연한 추가 데이터
    context String DEFAULT '{}'
) ENGINE = MergeTree()
PARTITION BY (service, toYYYYMMDD(timestamp))
ORDER BY (service, category, timestamp)
TTL timestamp + INTERVAL 90 DAY DELETE;
```

### Materialized View (빌드별 위반 집계)

```sql
CREATE TABLE project_sos.violation_rate_by_build (
    build_version LowCardinality(String),
    violation_type LowCardinality(String),
    hour DateTime,
    violation_count UInt64
) ENGINE = SummingMergeTree()
ORDER BY (build_version, violation_type, hour);

CREATE MATERIALIZED VIEW project_sos.violation_rate_by_build_mv
TO project_sos.violation_rate_by_build AS
SELECT
    build_version, violation_type,
    toStartOfHour(timestamp) AS hour,
    count() AS violation_count
FROM project_sos.service_events
WHERE record_type = 'violation'
GROUP BY build_version, violation_type, hour;
```

### Materialized View (서비스별 에러 집계)

```sql
CREATE TABLE project_sos.error_rate_by_service (
    service LowCardinality(String),
    category LowCardinality(String),
    hour DateTime,
    error_count UInt64
) ENGINE = SummingMergeTree()
ORDER BY (service, category, hour);

CREATE MATERIALIZED VIEW project_sos.error_rate_by_service_mv
TO project_sos.error_rate_by_service AS
SELECT
    service, category,
    toStartOfHour(timestamp) AS hour,
    count() AS error_count
FROM project_sos.service_events
WHERE level = 'ERROR'
GROUP BY service, category, hour;
```

---

## 데이터 수집: Vector

### 수집 경로

```
[Game Server] → 로컬 파일 (com.unity.logging) → Vector file source
[Room Server] → Docker stdout                 → Vector docker_logs source
[Chat Server] → Docker stdout                  → Vector docker_logs source
                                                        ↓
                                                   [ClickHouse]
                                                  service_events
```

### Vector 설정

```toml
# infra/vector/vector.toml

# ──────────────────────────────────────────
# Sources
# ──────────────────────────────────────────

[sources.game_logs]
type = "file"
include = ["/logs/game/**/*.log"]
read_from = "beginning"

[sources.docker_logs]
type = "docker_logs"
include_labels = ["vector.service"]

# ──────────────────────────────────────────
# Transforms: Game Server 로그 파싱
# ──────────────────────────────────────────

[transforms.parse_game]
type = "remap"
inputs = ["game_logs"]
source = '''
parsed, err = parse_regex(
  .message,
  r'^(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) \| (?P<level>\w+)\s+\| \[(?P<world>[SC]):(?P<category>\w+)\] (?P<msg>.*)'
)
if err != null { abort }

.timestamp = parse_timestamp!(parsed.ts, format: "%Y-%m-%d %H:%M:%S%.3f")
.service = "game"
.level = upcase!(parsed.level)
.world = parsed.world
.category = parsed.category
.message = parsed.msg

# key=value 패턴에서 구조화 필드 추출
.network_id = to_int(parse_regex(.message, r'networkId=(?P<v>\d+)').v) ?? 0
.ghost_id = to_int(parse_regex(.message, r'idx=(?P<v>\d+)').v) ?? 0

# 환경변수 기본값
.build_version = get_env_var("BUILD_VERSION") ?? "dev"
.session_id = get_env_var("SESSION_ID") ?? "local"
.server_id = get_env_var("SERVER_ID") ?? "dev-1"
.record_type = "log"
.violation_type = ""
.entity_type = ""
.team_id = 0
.context = "{}"
'''

# ──────────────────────────────────────────
# Transforms: C++ 서버 로그 파싱
# ──────────────────────────────────────────

[transforms.parse_cpp]
type = "remap"
inputs = ["docker_logs"]
source = '''
# Docker 라벨에서 서비스명 추출
.service = .label."vector.service" ?? "unknown"

# C++ 서버 로그 형식: "2026-03-15 14:40:45.030 | INFO | [Match] Client matched, wait_ms=1200"
parsed, err = parse_regex(
  .message,
  r'^(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) \| (?P<level>\w+)\s+\| \[(?P<category>\w+)\] (?P<msg>.*)'
)
if err != null { abort }

.timestamp = parse_timestamp!(parsed.ts, format: "%Y-%m-%d %H:%M:%S%.3f")
.level = upcase!(parsed.level)
.category = parsed.category
.message = parsed.msg

.build_version = get_env_var("BUILD_VERSION") ?? "dev"
.session_id = ""
.server_id = get_env_var("SERVER_ID") ?? "dev-1"
.record_type = "log"
.world = ""
.violation_type = ""
.entity_type = ""
.ghost_id = 0
.team_id = 0
.network_id = 0
.context = "{}"
'''

# ──────────────────────────────────────────
# Sink: ClickHouse
# ──────────────────────────────────────────

[sinks.clickhouse]
type = "clickhouse"
inputs = ["parse_game", "parse_cpp"]
endpoint = "http://clickhouse:8123"
database = "project_sos"
table = "service_events"
auth.strategy = "basic"
auth.user = "default"
auth.password = "${CH_PASSWORD}"
```

---

## C++ 서버 로그 출력 형식

Room Server와 Chat Server는 Unity와 동일한 형식으로 stdout에 출력한다.

```
<timestamp> | <LEVEL> | [<Category>] <message>, key=value, ...
```

### Room Server 로그 예시

```
2026-03-16 10:00:01.100 | INFO    | [Match] Client matched, wait_ms=1200, session=abc-123
2026-03-16 10:00:05.200 | INFO    | [Room] Room status, rooms=3, players=5
2026-03-16 10:00:10.300 | WARNING | [Auth] Token expired, network_id=5
2026-03-16 10:00:15.400 | ERROR   | [System] Redis connection lost
```

### Chat Server 로그 예시

```
2026-03-16 10:01:01.100 | INFO    | [Chat] Message sent, channel=ALL, session=abc-123
2026-03-16 10:01:05.200 | WARNING | [RateLimit] Limit hit, channel=LOBBY, rate_limit=1
2026-03-16 10:01:10.300 | INFO    | [Session] Client joined, channel=ALL
2026-03-16 10:01:15.400 | ERROR   | [System] Redis connection lost
```

### 카테고리 정리

| 서비스 | 카테고리 | 설명 |
|--------|---------|------|
| Game | Combat, Movement, Network, Economy, Wave, System | 기존 LogCategory enum |
| Room | Room, Auth, System | 방 관리, 인증, 시스템 |
| Chat | Chat, RateLimit, Session, System | 메시지, 제한, 세션, 시스템 |

---

## Grafana 데이터소스 프로비저닝

```yaml
# infra/grafana/datasources.yml
apiVersion: 1
datasources:
  - name: ClickHouse
    type: grafana-clickhouse-datasource
    access: proxy
    url: http://clickhouse:8123
    jsonData:
      defaultDatabase: project_sos
      username: default
    secureJsonData:
      password: ${CH_PASSWORD:-dev123}
```

---

## Grafana 대시보드

| 대시보드 | 주요 패널 |
|---------|----------|
| **Service Overview** | 서비스별 로그 볼륨 (시계열), 레벨별 분포, 에러율 추이 |
| **Game Violations** | 위반 유형별 발생 건수, 빌드별 위반 비율 비교, 엔티티 타입별 분포 |
| **Room Metrics** | 방 생성/참가 추이, 활성 방 수, 토큰 만료 건수 |
| **Chat Metrics** | 채널별 메시지 볼륨, Rate Limit 히트 빈도, 세션 수 추이 |
| **Error Dashboard** | 서비스별 에러 발생률, 에러 메시지별 빈도, 최근 에러 목록 |

---

## 알림 규칙

| 조건 | 심각도 | 설명 |
|------|--------|------|
| 새 빌드 배포 후 1시간 내 위반 건수 > 이전 빌드 평균 200% | Warning | 빌드 회귀 감지 |
| WallPenetration 5분간 10건 초과 | Warning | 물리 정합성 이상 |
| `level = 'ERROR'` 5분간 50건 초과 (전 서비스) | Critical | 에러 급증 |
| Room 방 생성 실패 5분간 10건 초과 | Warning | 방 관리 이상 |
| Redis connection lost (전 서비스) | Critical | 인프라 장애 |

### 알림 쿼리 예시

```sql
-- WallPenetration 5분 집계
SELECT count() AS cnt
FROM project_sos.service_events
WHERE service = 'game'
  AND violation_type = 'WallPenetration'
  AND timestamp >= now() - INTERVAL 5 MINUTE;

-- 빌드별 위반 비율 비교
SELECT
    build_version,
    count() AS violations,
    countIf(violation_type = 'WallPenetration') AS wall_pen
FROM project_sos.service_events
WHERE service = 'game'
  AND timestamp >= now() - INTERVAL 1 HOUR
GROUP BY build_version;

-- 전 서비스 에러 집계
SELECT service, count() AS errors
FROM project_sos.service_events
WHERE level = 'ERROR'
  AND timestamp >= now() - INTERVAL 5 MINUTE
GROUP BY service;

```

---

## 포렌식 조회 예시

ClickHouse에서 개별 레코드 조회도 ms 단위로 가능하다.

```sql
-- 특정 엔티티의 이벤트 추적
SELECT timestamp, level, category, message
FROM project_sos.service_events
WHERE service = 'game'
  AND ghost_id = 42
  AND session_id = 'abc-123'
ORDER BY timestamp DESC
LIMIT 50;

-- 특정 세션의 전체 이벤트 타임라인 (크로스 서비스)
SELECT timestamp, service, category, level, message
FROM project_sos.service_events
WHERE session_id = 'abc-123'
ORDER BY timestamp ASC;

-- 특정 시간대 Warning/Error 모아보기
SELECT timestamp, service, category, message
FROM project_sos.service_events
WHERE level IN ('WARNING', 'ERROR')
  AND timestamp BETWEEN '2026-03-15 14:40:00' AND '2026-03-15 14:45:00'
ORDER BY timestamp ASC;
```
