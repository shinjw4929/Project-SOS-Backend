# Step 5-5: Grafana 알림 규칙

## Context

에러/장애를 자동 감지하는 Grafana 알림을 파일 기반 프로비저닝으로 구성한다. 개발 단계에서는 Grafana 내장 알림(UI 알림 + 대시보드 배지)만 사용하고, 추후 Slack/Discord webhook 추가 가능.

---

## 프로비저닝 구조

```
infra/grafana/provisioning/alerting/
  alert-rules.yml       # 알림 규칙 5개
  contact-points.yml    # 알림 수신 채널 (Grafana 내장)
  policies.yml          # 알림 라우팅 정책
```

`docker-compose.yml` Grafana 볼륨에 추가:

```yaml
- ./infra/grafana/provisioning/alerting:/etc/grafana/provisioning/alerting
```

---

## 알림 규칙

| # | 알림 | 조건 | 평가 주기 | 대기 시간 | 심각도 |
|---|------|------|----------|----------|--------|
| 1 | Error Rate Spike | 5분간 ERROR 10건+ | 1분 | 5분 | critical |
| 2 | Room Creation Failure | 5분간 Room ERROR 5건+ | 1분 | 3분 | warning |
| 3 | Redis Connection Error | 5분간 Redis 에러 3건+ | 1분 | 2분 | critical |
| 4 | Chat Server Disconnected | 5분간 Room:Chat 경고 3건+ | 1분 | 3분 | warning |
| 5 | Internal Channel Error | 5분간 Internal 에러 3건+ | 1분 | 3분 | warning |

---

## 수정

### [ ] `infra/grafana/provisioning/alerting/alert-rules.yml` 생성

```yaml
apiVersion: 1
groups:
  - orgId: 1
    name: SOS-Backend-Alerts
    folder: Project-SOS
    interval: 1m
    rules:
      - uid: error-rate-spike
        title: "Error Rate Spike"
        condition: C
        data:
          - refId: A
            datasourceUid: clickhouse-ds
            model:
              rawSql: >
                SELECT count() AS error_count
                FROM project_sos.service_events
                WHERE timestamp >= now() - INTERVAL 5 MINUTE
                AND level = 'ERROR'
          - refId: C
            datasourceUid: __expr__
            model:
              type: threshold
              conditions:
                - evaluator: { type: gt, params: [10] }
        for: 5m
        labels: { severity: critical }
        annotations:
          summary: "5분간 에러 {{ $values.A }} 건 발생"
      - uid: room-creation-failure
        title: "Room Creation Failure"
        condition: C
        data:
          - refId: A
            datasourceUid: clickhouse-ds
            model:
              rawSql: >
                SELECT count() AS error_count
                FROM project_sos.service_events
                WHERE timestamp >= now() - INTERVAL 5 MINUTE
                AND service = 'room' AND level = 'ERROR'
          - refId: C
            datasourceUid: __expr__
            model:
              type: threshold
              conditions:
                - evaluator: { type: gt, params: [5] }
        for: 3m
        labels: { severity: warning }
        annotations:
          summary: "5분간 Room 에러 {{ $values.A }} 건 발생"

      - uid: redis-connection-error
        title: "Redis Connection Error"
        condition: C
        data:
          - refId: A
            datasourceUid: clickhouse-ds
            model:
              rawSql: >
                SELECT count() AS error_count
                FROM project_sos.service_events
                WHERE timestamp >= now() - INTERVAL 5 MINUTE
                AND level = 'ERROR'
                AND message LIKE '%Redis%'
          - refId: C
            datasourceUid: __expr__
            model:
              type: threshold
              conditions:
                - evaluator: { type: gt, params: [3] }
        for: 2m
        labels: { severity: critical }
        annotations:
          summary: "5분간 Redis 에러 {{ $values.A }} 건 발생"

      - uid: chat-server-disconnected
        title: "Chat Server Disconnected"
        condition: C
        data:
          - refId: A
            datasourceUid: clickhouse-ds
            model:
              rawSql: >
                SELECT count() AS warn_count
                FROM project_sos.service_events
                WHERE timestamp >= now() - INTERVAL 5 MINUTE
                AND category = 'Room:Chat'
                AND level IN ('WARNING', 'ERROR')
          - refId: C
            datasourceUid: __expr__
            model:
              type: threshold
              conditions:
                - evaluator: { type: gt, params: [3] }
        for: 3m
        labels: { severity: warning }
        annotations:
          summary: "5분간 Room:Chat 경고 {{ $values.A }} 건 (Vector 정규식 수정 선행 필요: Step 5-1)"

      - uid: internal-channel-error
        title: "Internal Channel Error"
        condition: C
        data:
          - refId: A
            datasourceUid: clickhouse-ds
            model:
              rawSql: >
                SELECT count() AS error_count
                FROM project_sos.service_events
                WHERE timestamp >= now() - INTERVAL 5 MINUTE
                AND category IN ('Room:Internal', 'Chat:Internal')
                AND level = 'ERROR'
          - refId: C
            datasourceUid: __expr__
            model:
              type: threshold
              conditions:
                - evaluator: { type: gt, params: [3] }
        for: 3m
        labels: { severity: warning }
        annotations:
          summary: "5분간 Internal 에러 {{ $values.A }} 건 (Vector 정규식 수정 선행 필요: Step 5-1)"
```

> **선행 조건**: 규칙 4, 5는 콜론 포함 카테고리(`Room:Chat`, `Room:Internal`, `Chat:Internal`)를 ClickHouse에서 조회한다. Step 5-1의 Vector 정규식 수정(`\w+` -> `[\w:]+`)이 완료되지 않으면 해당 카테고리 로그가 ClickHouse에 저장되지 않아 알림이 작동하지 않는다.

### [ ] `infra/grafana/provisioning/alerting/contact-points.yml` 생성

```yaml
apiVersion: 1
contactPoints:
  - orgId: 1
    name: grafana-default-email
    receivers:
      - uid: default-email
        type: email
        settings:
          addresses: ""  # Grafana UI에서 설정하거나 환경변수로 주입
```

### [ ] `infra/grafana/provisioning/alerting/policies.yml` 생성

```yaml
apiVersion: 1
policies:
  - orgId: 1
    receiver: grafana-default-email
    group_by: ['alertname', 'severity']
    group_wait: 30s
    group_interval: 5m
    repeat_interval: 12h
```

### [ ] `docker-compose.yml` Grafana 볼륨 추가

```yaml
grafana:
  volumes:
    - ... # 기존
    - ./infra/grafana/provisioning/alerting:/etc/grafana/provisioning/alerting
```

---

## 검증

- Grafana Alerting 탭에서 5개 규칙이 표시되는지 확인
- 규칙 상태가 `OK` / `Pending` / `Firing` 중 하나인지 확인
- 의도적으로 에러 로그를 발생시켜 알림이 Firing 상태로 전환되는지 확인 (선택)
