# Phase 1: 로비 세션 추적 인프라

## 목표

클라이언트 연결의 현재 상태(로비/방 내)를 추적하여, Push 브로드캐스트 대상을 식별할 수 있는 기반을 구축한다.

## 전제 조건

없음 (첫 번째 Phase)

## 작업 목록

### 순차 작업

- [x] RoomManager에 로비 세션 자료구조 및 메서드 추가
- [x] ClientSession에서 로비 등록/해제 호출 연결
- [x] RoomManager 핸들러에서 상태 전이 시 로비 추적 갱신
- [x] main.cpp에서 io_context를 RoomManager에 전달 (Phase 2 선행)

## 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `src/room/room/RoomManager.h` | io_context 참조 + lobby_sessions_ 맵 + public 메서드 추가 |
| `src/room/room/RoomManager.cpp` | 생성자 수정 + addLobbySession/removeLobbySession 구현 + 핸들러 내 로비 전이 로직 |
| `src/room/server/ClientSession.cpp` | start()에서 addLobbySession, close()에서 removeLobbySession, kLeaveRoom에서 addLobbySession |
| `src/room/main.cpp` | RoomManager 생성 시 io_context 전달 |

## 상세 설계

### 자료구조

```cpp
// RoomManager.h
std::unordered_map<ClientSession*, std::weak_ptr<ClientSession>> lobby_sessions_;
```

- Key: raw pointer (O(1) 조회/삭제)
- Value: weak_ptr (안전한 메시지 전송, 수명 관리)

### 상태 전이 매트릭스

| 이벤트 | 로비 동작 | 위치 |
|--------|----------|------|
| 클라이언트 연결 | addLobbySession | ClientSession::start() |
| CreateRoom 성공 | removeLobbySession | RoomManager::handleCreateRoom |
| JoinRoom 성공 | removeLobbySession | RoomManager::handleJoinRoom |
| LeaveRoom (명시적) | addLobbySession | ClientSession::processMessage kLeaveRoom |
| 호스트 퇴장 → 방 폐쇄 | 잔여 멤버 addLobbySession | RoomManager::removeRoom |
| 게임 서버 단절 → 방 폐쇄 | 잔여 멤버 addLobbySession | RoomManager::handleGameServerDisconnect |
| 게임 종료 (SlotReleased) | sessions_[player_id].lock() 성공 시 addLobbySession (sessions_.erase 전) | RoomManager::handleSlotReleased |
| 클라이언트 연결 해제 | removeLobbySession | ClientSession::close() (`if (!player_id_.empty())` 블록 바깥, 무조건 호출) |

### lobby 복귀 시 weak_ptr lock 패턴

`removeRoom`, `handleGameServerDisconnect`, `handleSlotReleased`에서 잔여 멤버를 lobby에 복귀시킬 때, 해당 플레이어의 `sessions_[player_id]` weak_ptr을 `lock()`하여 유효한 경우에만 `addLobbySession`을 호출한다. 게임 진행 중 또는 게임 서버 단절 시 클라이언트가 이미 Room Server 연결을 끊었을 수 있으므로, lock 실패 시 skip한다.

```cpp
// removeRoom / handleGameServerDisconnect / handleSlotReleased 공통 패턴
if (auto it = sessions_.find(player_id); it != sessions_.end()) {
    if (auto session = it->second.lock()) {
        addLobbySession(session);
    }
}
```

### RoomManager 생성자 변경

```cpp
// Before
RoomManager(uint32_t max_rooms, uint32_t max_players_per_room,
            std::shared_ptr<SessionStore> session_store,
            std::string game_server_host, uint16_t game_server_port,
            std::shared_ptr<ChatServerChannel> chat_channel = nullptr);

// After
RoomManager(boost::asio::io_context& io_context,
            uint32_t max_rooms, uint32_t max_players_per_room,
            std::shared_ptr<SessionStore> session_store,
            std::string game_server_host, uint16_t game_server_port,
            std::shared_ptr<ChatServerChannel> chat_channel = nullptr);
```

## 검증

- [x] 빌드 성공
- [x] 기존 유닛 테스트 통과
- [ ] 로비 세션 등록/해제 로그 확인 (spdlog::debug) — 수동 테스트 시 확인
