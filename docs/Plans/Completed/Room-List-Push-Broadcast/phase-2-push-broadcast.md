# Phase 2: 디바운스 방 목록 Push 브로드캐스트

## 목표

방 목록에 변동이 생길 때마다 로비 상태의 모든 클라이언트에게 `RoomListResponse`를 Push 전송한다. 300ms 디바운스로 연속 이벤트를 병합하여 브로드캐스트 빈도를 제어한다.

## 전제 조건

Phase 1 완료 (로비 세션 추적 인프라)

## 작업 목록

### 순차 작업

- [x] RoomManager에 디바운스 타이머, 브로드캐스트, stop() 메서드 추가
- [x] 방 변동 이벤트 지점에 notifyRoomListChanged() 호출 삽입
- [x] main.cpp signal handler에서 room_manager->stop() 호출 추가
- [x] Room Server 시스템 문서 업데이트

## 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `src/room/room/RoomManager.h` | steady_timer + bool 멤버 + private 메서드 2개 + public stop() 선언 |
| `src/room/room/RoomManager.cpp` | 생성자에서 타이머 초기화 + notifyRoomListChanged/broadcastRoomListToLobby/stop 구현 + 이벤트 지점 호출 |
| `src/room/main.cpp` | signal handler에서 room_manager->stop() 호출 추가 |
| `docs/Systems/Room Server.md` | 방 목록 Push 브로드캐스트 섹션 추가 |

## 상세 설계

### 디바운스 타이머

```cpp
// RoomManager.h (private members)
static constexpr auto kLobbyBroadcastDelay = std::chrono::milliseconds(300);
static constexpr uint32_t kLobbyBroadcastPageSize = 20;

boost::asio::steady_timer lobby_broadcast_timer_;
bool lobby_broadcast_pending_ = false;
```

### notifyRoomListChanged

```cpp
void RoomManager::notifyRoomListChanged() {
    if (lobby_sessions_.empty()) return;
    if (lobby_broadcast_pending_) return;

    lobby_broadcast_pending_ = true;
    lobby_broadcast_timer_.expires_after(kLobbyBroadcastDelay);
    lobby_broadcast_timer_.async_wait([this](boost::system::error_code ec) {
        lobby_broadcast_pending_ = false;
        if (ec) return;
        broadcastRoomListToLobby();
    });
}
```

- 로비 세션이 없으면 즉시 반환 (불필요한 타이머 방지)
- 이미 타이머가 동작 중이면 중복 시작하지 않음
- 300ms 후 broadcastRoomListToLobby 실행
- `this` 캡처 안전 근거: timer는 RoomManager 멤버 → 소멸자에서 cancel → 콜백 ec=operation_aborted로 반환. shared_from_this() 불필요

### broadcastRoomListToLobby

```cpp
void RoomManager::broadcastRoomListToLobby() {
    if (lobby_sessions_.empty()) return;

    // ROOM_WAITING 방 수집
    std::vector<std::shared_ptr<Room>> waiting_rooms;
    for (const auto& [id, room] : rooms_) {
        if (room->state() == sos::room::ROOM_WAITING) {
            waiting_rooms.push_back(room);
        }
    }

    // RoomListResponse 빌드 (page=0, size=kLobbyBroadcastPageSize)
    sos::room::Envelope envelope;
    auto* response = envelope.mutable_room_list_response();
    response->set_total_rooms(static_cast<uint32_t>(waiting_rooms.size()));
    uint32_t count = std::min(static_cast<uint32_t>(waiting_rooms.size()), kLobbyBroadcastPageSize);
    for (uint32_t i = 0; i < count; ++i) {
        *response->add_rooms() = waiting_rooms[i]->toRoomSummary();
    }

    // 로비 세션에 전송 + 만료 세션 정리
    std::vector<ClientSession*> expired;
    for (auto& [ptr, weak] : lobby_sessions_) {
        if (auto session = weak.lock()) {
            session->send(envelope);
        } else {
            expired.push_back(ptr);
        }
    }
    for (auto* ptr : expired) {
        lobby_sessions_.erase(ptr);
    }
}
```

### stop (Graceful Shutdown)

```cpp
void RoomManager::stop() {
    lobby_broadcast_timer_.cancel();
}
```

- signal handler에서 `room_manager->stop()` 호출
- 타이머 취소 → 콜백 ec=operation_aborted → 즉시 반환
- io_context가 불필요하게 300ms 유지되는 것을 방지

### 호출 지점

| 이벤트 | 위치 | 호출 시점 |
|--------|------|----------|
| 방 생성 | handleCreateRoom | 방 생성 + 응답 전송 후 |
| 플레이어 참가 | handleJoinRoom | 참가 처리 + 응답 전송 후 |
| 플레이어 퇴장 (비호스트) | handleLeaveRoom | RoomUpdate 전송 후 |
| 호스트 퇴장 → 방 삭제 | handleLeaveRoom | removeRoom 후 |
| 게임 시작 | handleStartGame | 상태 변경 + GameStart 전송 후 |
| 게임 종료 → 빈 방 삭제 | handleSlotReleased | 방 삭제 후 (선택적: 삭제되는 방은 IN_GAME 상태로 WAITING 목록에 영향 없음) |
| 게임 서버 단절 | handleGameServerDisconnect | 방 일괄 삭제 후 (선택적: 삭제되는 방은 IN_GAME 상태로 WAITING 목록에 영향 없음) |

## 검증

- [x] 빌드 성공
- [x] 기존 유닛 테스트 통과
- [ ] 수동 테스트: 방 생성 시 다른 로비 클라이언트에 자동 갱신 확인
- [ ] 수동 테스트: 방 참가/퇴장 시 로비 클라이언트에 인원 수 갱신 확인
- [ ] 수동 테스트: 게임 시작 시 로비에서 해당 방 제거 확인
- [ ] 수동 테스트: 연속 이벤트 시 디바운스 동작 확인 (브로드캐스트 병합)
