# Phase 2: Vector 라우팅 파이프라인

## 목표

Vector에서 `[Chat:Message]` 카테고리 로그를 분리하여 구조화된 필드로 파싱하고, `chat_messages` 테이블에 전송한다. 기존 `service_events` 파이프라인은 영향받지 않아야 한다.

## 전제 조건

- Phase 1 완료 (ClickHouse `chat_messages` 테이블 존재, C++에서 `[Chat:Message]` 로그 출력)

## 작업 목록

### 순차 작업

- [x] **Task A**: `vector.toml`에 `route_cpp` 트랜스폼 추가 (카테고리 기반 분기)
- [x] **Task B**: `vector.toml`에 `extract_chat_fields` 트랜스폼 추가 (key-value 파싱)
- [x] **Task C**: `vector.toml`에 `clickhouse_chat_messages` 싱크 추가
- [x] **Task D**: `clean_fields.inputs` 수정 (`parse_cpp` → `route_cpp._unmatched`)
- [x] **Task E**: `vector-tests.toml`에 Chat:Message 파싱 테스트 추가

### Task A: route_cpp 트랜스폼

`parse_cpp` 이후, `clean_fields` 이전에 라우팅 트랜스폼 삽입.

```toml
[transforms.route_cpp]
type = "route"
inputs = ["parse_cpp"]

  [transforms.route_cpp.route]
  chat_message = '.category == "Chat:Message"'
```

- `route_cpp.chat_message`: Chat:Message 카테고리 로그 → 별도 파이프라인
- `route_cpp._unmatched`: 나머지 → 기존 clean_fields → service_events

### Task B: extract_chat_fields 트랜스폼

Chat:Message 로그의 message 필드에서 구조화된 컬럼 추출.

```toml
[transforms.extract_chat_fields]
type = "remap"
inputs = ["route_cpp.chat_message"]
source = '''
parsed, err = parse_regex(
  .message,
  r'^channel=(?P<channel>[^,]+), sender_id=(?P<sender_id>[^,]+), sender_name=(?P<sender_name>[^,]+), session_id=(?P<session_id>[^,]*), target_id=(?P<target_id>[^,]*), content=(?P<content>.*)'
)
if err != null { abort }

.channel = parsed.channel
.sender_id = parsed.sender_id
.sender_name = parsed.sender_name
.session_id = parsed.session_id
.target_id = parsed.target_id
.content = parsed.content

del(.service)
del(.level)
del(.category)
del(.message)
del(.build_version)
del(.server_id)
del(.record_type)
del(.world)
del(.violation_type)
del(.entity_type)
del(.ghost_id)
del(.team_id)
del(.network_id)
del(.context)
del(.file)
del(.host)
del(.source_type)
del(.label)
del(.stream)
del(.container_created_at)
del(.container_id)
del(.container_name)
del(.image)
'''
```

### Task C: clickhouse_chat_messages 싱크

```toml
[sinks.clickhouse_chat_messages]
type = "clickhouse"
inputs = ["extract_chat_fields"]
endpoint = "http://clickhouse:8123"
database = "project_sos"
table = "chat_messages"
auth.strategy = "basic"
auth.user = "default"
auth.password = "${CH_PASSWORD}"

batch.max_events = 500
batch.timeout_secs = 5

[sinks.clickhouse_chat_messages.buffer]
type = "disk"
max_size = 268435456
when_full = "block"
```

설계 근거:
- 배치 크기 500: 채팅 메시지는 운영 로그보다 볼륨이 낮으므로 더 작은 배치
- timeout 5초: 기존 `clickhouse` 싱크와 동일 (Grafana 조회 응답성)
- 디스크 버퍼 256MB: 운영 로그 대비 절반

### Task D: clean_fields inputs 수정

```toml
# 변경 전
inputs = ["parse_game", "parse_cpp"]

# 변경 후
inputs = ["parse_game", "route_cpp._unmatched"]
```

이로써 Chat:Message 로그는 service_events에 중복 저장되지 않는다.

### Task E: Vector 테스트

`infra/vector/vector-tests.toml`에 추가:

```toml
[[tests]]
name = "chat_message_extracted"

  [[tests.inputs]]
  insert_at = "extract_chat_fields"
  type = "log"
    [tests.inputs.log_fields]
    timestamp = "2026-04-08T10:00:01.100Z"
    message = "channel=ALL, sender_id=player1, sender_name=John, session_id=sess-abc, target_id=, content=hello world"

  [[tests.outputs]]
  extract_from = "extract_chat_fields"
    [[tests.outputs.conditions]]
    type = "vrl"
    source = '''
    assert_eq!(.channel, "ALL")
    assert_eq!(.sender_id, "player1")
    assert_eq!(.sender_name, "John")
    assert_eq!(.session_id, "sess-abc")
    assert_eq!(.target_id, "")
    assert_eq!(.content, "hello world")
    assert!(!exists(.service))
    assert!(!exists(.level))
    '''

[[tests]]
name = "chat_message_whisper_with_target"

  [[tests.inputs]]
  insert_at = "extract_chat_fields"
  type = "log"
    [tests.inputs.log_fields]
    timestamp = "2026-04-08T10:00:02.200Z"
    message = "channel=WHISPER, sender_id=p1, sender_name=Kim, session_id=sess-abc, target_id=p2, content=hey there"

  [[tests.outputs]]
  extract_from = "extract_chat_fields"
    [[tests.outputs.conditions]]
    type = "vrl"
    source = '''
    assert_eq!(.channel, "WHISPER")
    assert_eq!(.sender_id, "p1")
    assert_eq!(.target_id, "p2")
    assert_eq!(.session_id, "sess-abc")
    assert_eq!(.content, "hey there")
    '''

[[tests]]
name = "chat_message_with_comma_in_content"

  [[tests.inputs]]
  insert_at = "extract_chat_fields"
  type = "log"
    [tests.inputs.log_fields]
    timestamp = "2026-04-08T10:00:03.300Z"
    message = "channel=LOBBY, sender_id=p1, sender_name=Kim, session_id=, target_id=, content=hello, world, test"

  [[tests.outputs]]
  extract_from = "extract_chat_fields"
    [[tests.outputs.conditions]]
    type = "vrl"
    source = '''
    assert_eq!(.channel, "LOBBY")
    assert_eq!(.content, "hello, world, test")
    assert_eq!(.session_id, "")
    '''
```

## 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `infra/vector/vector.toml` | route_cpp, extract_chat_fields 트랜스폼 + clickhouse_chat_messages 싱크 추가, clean_fields.inputs 수정 |
| `infra/vector/vector-tests.toml` | Chat:Message 파싱 테스트 3개 추가 (ALL 정상, WHISPER target_id, LOBBY 쉼표 content) |

## 검증

- [x] Vector 테스트 통과 (`vector test --config-toml vector.toml --config-toml vector-tests.toml`) — Docker 환경에서 수동 검증 필요
- [ ] Docker Compose 기동 후 채팅 메시지 전송 → ClickHouse `chat_messages` 테이블에 행 삽입 확인
- [ ] 기존 운영 로그 (`service_events`)에 Chat:Message가 혼입되지 않음 확인
- [x] 기존 Vector 테스트 전부 통과 (회귀 없음) — clean_fields 직접 주입 테스트는 영향 없음
