CREATE DATABASE IF NOT EXISTS project_sos;

-- 전 서비스 통합 이벤트 테이블
CREATE TABLE project_sos.service_events (
    -- 공통 필드
    timestamp DateTime64(3, 'UTC'),
    service LowCardinality(String),
    build_version LowCardinality(String),
    session_id String DEFAULT '',
    server_id LowCardinality(String) DEFAULT 'dev',
    level LowCardinality(String),
    category LowCardinality(String),
    record_type LowCardinality(String) DEFAULT 'log',
    message String,

    -- Game Server 전용
    world LowCardinality(String) DEFAULT '',
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
TTL toDate(timestamp) + INTERVAL 90 DAY DELETE;

-- 빌드별 위반 집계 MV
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

-- 서비스별 에러 집계 MV
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

-- 채팅 메시지 영구 저장 테이블
CREATE TABLE project_sos.chat_messages (
    timestamp   DateTime64(3, 'UTC'),
    session_id  String DEFAULT '',
    channel     LowCardinality(String),
    sender_id   String,
    sender_name String,
    target_id   String DEFAULT '',
    content     String
) ENGINE = MergeTree()
PARTITION BY toYYYYMMDD(timestamp)
ORDER BY (session_id, timestamp)
TTL toDate(timestamp) + INTERVAL 90 DAY DELETE;
