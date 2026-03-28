# C++ 패턴 탐색 가이드

구현 전 기존 코드에서 동일/유사 패턴을 찾아 참조하는 절차.

---

## 1. 구현 유형별 탐색 전략

### TCP 서버/세션

| 탐색 대상 | 참조 파일 |
|----------|----------|
| TCP acceptor + async accept 루프 | `src/room/server/RoomServer.cpp`, `src/chat/server/ChatServer.cpp` |
| 클라이언트 세션 (read/write/heartbeat) | `src/room/server/ClientSession.cpp`, `src/chat/server/ChatSession.cpp` |
| 내부 채널 서버 | `src/room/internal/GameServerChannel.cpp`, `src/chat/internal/InternalChannel.cpp` |
| 내부 채널 세션 | `src/room/internal/GameServerSession.cpp`, `src/chat/internal/InternalSession.cpp` |
| TCP 클라이언트 (outgoing) | `src/room/internal/ChatServerChannel.cpp` |

### Protobuf 메시지 처리

| 탐색 대상 | 참조 파일 |
|----------|----------|
| Envelope dispatch (switch payload_case) | `ClientSession::processMessage`, `ChatSession::processMessage` |
| 응답 빌드 + send | `RoomManager::handleCreateRoom`, `ChannelManager::handleAuth` |
| 내부 프로토콜 메시지 | `GameServerSession::processMessage`, `InternalSession::processMessage` |

### Redis 연동

| 탐색 대상 | 참조 파일 |
|----------|----------|
| String get/set/setex | `SessionStore::createToken`, `SessionStore::validateToken` |
| Hash 조작 (hset/hget/hgetall) | `ChannelManager::handleAuth`, `ChannelManager::handleSessionCreated` |
| List 조작 (lpush/ltrim/lrange) | `ChannelManager::saveToHistory`, `ChannelManager::sendHistory` |
| TTL 기반 Rate Limiting | `RateLimiter::allow` |

### 비즈니스 로직

| 탐색 대상 | 참조 파일 |
|----------|----------|
| 방 관리 (생성/참가/퇴장) | `RoomManager.cpp` |
| 채널 라우팅 (로비/세션/귓속말) | `ChannelManager.cpp` |
| 토큰 발급/검증 | `SessionStore.cpp`, `GameServerSession.cpp` |
| 재연결/자동 복구 | `ChatServerChannel::scheduleReconnect` |

### 유틸리티/공통

| 탐색 대상 | 참조 파일 |
|----------|----------|
| 설정값 읽기 | `Config.cpp` (accessor 메서드 패턴) |
| 로깅 | `Logger.cpp` (init), 각 모듈의 `spdlog::info/warn/error` 호출 |
| UUID 생성 | `UuidGenerator.cpp` |
| Codec 인코딩/디코딩 | `Codec.h` (템플릿) |

---

## 2. 탐색 규칙

1. **참조 패턴 수**: 최소 1개, 최대 3개. 3개 이상 찾지 않는다
2. **우선순위**: 수정 대상 파일 내 기존 코드 > 같은 모듈 내 유사 파일 > 다른 모듈의 동일 패턴
3. **패턴 없는 경우**: CLAUDE.md Development Guidelines 기준으로 구현
4. **외부 라이브러리 패턴**: Boost.Asio, Protobuf, redis-plus-plus, spdlog는 기존 사용법을 따른다

---

## 3. 패턴 추출 항목

| 항목 | 확인 내용 |
|------|----------|
| **구조** | 클래스 선언, 멤버 순서, 생성자 패턴, shared_from_this 사용 |
| **비동기 I/O** | async_read/write 콜백 체인, self 캡처, 에러 처리 |
| **에러 처리** | boost::system::error_code 패턴, try-catch 범위, 로깅 수준 |
| **네이밍** | 메서드명 (handle*, do*, send*, broadcast*), 멤버 접미사 (_) |
| **Config 사용** | config.accessor() 호출, 기본값 패턴 |

---

## 4. 적용 원칙

1. 참조 패턴의 구조를 따른다. "더 나은 방법"이 있어도 일관성 우선
2. 파일 배치는 기존 디렉토리 구조를 따른다 (server/, room/, internal/, channel/)
3. 프로젝트 컨벤션이 버그를 유발하는 경우에만 벗어나되, 사유를 주석으로 남긴다
