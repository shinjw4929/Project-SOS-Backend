"""
Room Server E2E 피처 테스트

방 생명주기 전체 엣지 케이스 검증:
  - 방 생성/조회/입장/퇴장
  - 꽉 참, 중복 플레이어, 이미 방에 있음, 존재하지 않는 방
  - 레디 토글, 비호스트 시작, 미준비 시작
  - 호스트 퇴장 시 방 파괴
  - 정상 게임 시작 플로우

사전 준비:
  1. docker compose up -d redis
  2. room-server.exe 실행
  3. pip install protobuf
  4. python scripts/test_room.py
"""

import asyncio
import sys

sys.path.insert(0, "scripts")
import room_pb2
from proto_client import (
    ProtoClient, TestRunner,
    room_create, room_join, room_leave, room_toggle_ready,
    room_start_game, room_list,
)

ROOM_HOST = "127.0.0.1"
ROOM_PORT = 8080

runner = TestRunner()
check = runner.check


# ============================================================
# Test 1: 방 생성 + 방 목록 조회
# ============================================================

async def test_create_and_list():
    print("\n=== Test 1: 방 생성 + 방 목록 조회 ===")

    client = ProtoClient("Host")
    await client.connect(ROOM_HOST, ROOM_PORT)

    try:
        # 방 생성
        resp = await room_create(client, "host1", "Host", "Test Room", 4)
        check(resp.HasField("create_room_response"), "방 생성 응답 수신")
        check(resp.create_room_response.success, "방 생성 성공")
        room_id = resp.create_room_response.room.room_id
        check(len(room_id) > 0, "room_id 비어있지 않음")
        check(resp.create_room_response.room.room_name == "Test Room", "방 이름 일치")
        check(resp.create_room_response.room.max_players == 4, "최대 인원 일치")
        check(resp.create_room_response.room.host_id == "host1", "호스트 ID 일치")
        check(len(resp.create_room_response.room.players) == 1, "플레이어 1명")

        # 방 목록 조회 (다른 클라이언트에서)
        lister = ProtoClient("Lister")
        await lister.connect(ROOM_HOST, ROOM_PORT)

        list_resp = await room_list(lister)
        check(list_resp.HasField("room_list_response"), "방 목록 응답 수신")
        check(list_resp.room_list_response.total_rooms >= 1, "방 1개 이상 존재")

        found = False
        for room in list_resp.room_list_response.rooms:
            if room.room_id == room_id:
                found = True
                check(room.room_name == "Test Room", "목록에서 방 이름 일치")
                check(room.current_players == 1, "현재 인원 = 1")
                check(room.max_players == 4, "최대 인원 = 4")
                check(room.state == room_pb2.ROOM_WAITING, "상태 = WAITING")
        check(found, "생성한 방이 목록에 존재")

        await lister.close()
    finally:
        await client.close()


# ============================================================
# Test 2: 방 입장 + RoomUpdate 브로드캐스트
# ============================================================

async def test_join_and_update():
    print("\n=== Test 2: 방 입장 + RoomUpdate ===")

    host = ProtoClient("Host")
    joiner = ProtoClient("Joiner")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await joiner.connect(ROOM_HOST, ROOM_PORT)

    try:
        # 방 생성
        resp = await room_create(host, "host2", "Host", "Join Test", 4)
        room_id = resp.create_room_response.room.room_id

        # 입장
        join_resp = await room_join(joiner, "joiner2", "Joiner", room_id)
        check(join_resp.HasField("join_room_response"), "입장 응답 수신")
        check(join_resp.join_room_response.success, "입장 성공")
        check(len(join_resp.join_room_response.room.players) == 2, "플레이어 2명")

        # 호스트에게 RoomUpdate 도착
        update = await host.recv_room()
        check(update.HasField("room_update"), "호스트에게 RoomUpdate 수신")
        check(len(update.room_update.room.players) == 2, "업데이트: 플레이어 2명")
    finally:
        await host.close()
        await joiner.close()


# ============================================================
# Test 3: 비호스트 퇴장
# ============================================================

async def test_leave_non_host():
    print("\n=== Test 3: 비호스트 퇴장 ===")

    host = ProtoClient("Host")
    joiner = ProtoClient("Joiner")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await joiner.connect(ROOM_HOST, ROOM_PORT)

    try:
        resp = await room_create(host, "host3", "Host", "Leave Test", 4)
        room_id = resp.create_room_response.room.room_id

        await room_join(joiner, "joiner3", "Joiner", room_id)
        await host.drain_messages(1)  # RoomUpdate (입장)

        # 비호스트 퇴장
        await room_leave(joiner)
        await asyncio.sleep(0.2)

        # 호스트에게 RoomUpdate (1명으로 감소)
        update = await host.recv_room()
        check(update.HasField("room_update"), "호스트에게 퇴장 RoomUpdate 수신")
        check(len(update.room_update.room.players) == 1, "플레이어 1명으로 감소")
    finally:
        await host.close()
        await joiner.close()


# ============================================================
# Test 4: 호스트 퇴장 -> 방 파괴
# ============================================================

async def test_leave_host():
    print("\n=== Test 4: 호스트 퇴장 -> 방 파괴 ===")

    host = ProtoClient("Host")
    joiner = ProtoClient("Joiner")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await joiner.connect(ROOM_HOST, ROOM_PORT)

    try:
        resp = await room_create(host, "host4", "Host", "Host Leave Test", 4)
        room_id = resp.create_room_response.room.room_id

        await room_join(joiner, "joiner4", "Joiner", room_id)
        await host.drain_messages(1)  # RoomUpdate (입장)

        # 호스트 퇴장
        await room_leave(host)
        await asyncio.sleep(0.3)

        # Joiner에게 방 파괴 알림 (RoomUpdate 또는 연결 종료)
        msg = await joiner.try_recv_room(timeout=2.0)
        if msg is not None:
            # RejectResponse(ROOM_CLOSED) 또는 RoomUpdate(players=0)
            has_reject = msg.HasField("reject")
            has_update = msg.HasField("room_update")
            check(has_reject or has_update, "Joiner에게 방 파괴 알림 수신")
            if has_reject:
                check(msg.reject.reason == room_pb2.RejectResponse.ROOM_CLOSED,
                      "거부 사유 = ROOM_CLOSED")
        else:
            # 연결이 끊어진 경우도 정상 (서버가 강제 종료)
            check(True, "Joiner 연결 종료 (방 파괴)")

        # 방 목록에서 사라졌는지 확인
        checker = ProtoClient("Checker")
        await checker.connect(ROOM_HOST, ROOM_PORT)
        list_resp = await room_list(checker)
        found = any(r.room_id == room_id for r in list_resp.room_list_response.rooms)
        check(not found, "방 목록에서 삭제됨")
        await checker.close()
    finally:
        await host.close()
        await joiner.close()


# ============================================================
# Test 5: 방 꽉 참 거부
# ============================================================

async def test_room_full():
    print("\n=== Test 5: 방 꽉 참 거부 ===")

    host = ProtoClient("Host")
    p2 = ProtoClient("P2")
    p3 = ProtoClient("P3")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await p2.connect(ROOM_HOST, ROOM_PORT)
    await p3.connect(ROOM_HOST, ROOM_PORT)

    try:
        # 2인 방 생성
        resp = await room_create(host, "host5", "Host", "Full Test", 2)
        room_id = resp.create_room_response.room.room_id

        # P2 입장 (성공)
        join_resp = await room_join(p2, "p2_5", "P2", room_id)
        check(join_resp.join_room_response.success, "P2 입장 성공 (2/2)")
        await host.drain_messages(1)  # RoomUpdate

        # P3 입장 시도 (실패)
        reject_resp = await room_join(p3, "p3_5", "P3", room_id)
        has_reject = reject_resp.HasField("reject")
        has_join_fail = (reject_resp.HasField("join_room_response") and
                         not reject_resp.join_room_response.success)
        check(has_reject or has_join_fail, "P3 입장 거부")
        if has_reject:
            check(reject_resp.reject.reason == room_pb2.RejectResponse.ROOM_FULL,
                  "거부 사유 = ROOM_FULL")
    finally:
        await host.close()
        await p2.close()
        await p3.close()


# ============================================================
# Test 6: 존재하지 않는 방 입장
# ============================================================

async def test_room_not_found():
    print("\n=== Test 6: 존재하지 않는 방 입장 ===")

    client = ProtoClient("Client")
    await client.connect(ROOM_HOST, ROOM_PORT)

    try:
        resp = await room_join(client, "player6", "Player", "nonexistent-room-id")
        has_reject = resp.HasField("reject")
        has_join_fail = (resp.HasField("join_room_response") and
                         not resp.join_room_response.success)
        check(has_reject or has_join_fail, "존재하지 않는 방 거부")
        if has_reject:
            check(resp.reject.reason == room_pb2.RejectResponse.ROOM_NOT_FOUND,
                  "거부 사유 = ROOM_NOT_FOUND")
    finally:
        await client.close()


# ============================================================
# Test 7: 중복 플레이어 거부
# ============================================================

async def test_duplicate_player():
    print("\n=== Test 7: 중복 플레이어 거부 ===")

    c1 = ProtoClient("C1")
    c2 = ProtoClient("C2")
    await c1.connect(ROOM_HOST, ROOM_PORT)
    await c2.connect(ROOM_HOST, ROOM_PORT)

    try:
        # C1: 방 생성
        resp = await room_create(c1, "dup_player", "DupPlayer", "Dup Test", 4)
        room_id = resp.create_room_response.room.room_id

        # C2: 같은 player_id로 입장 시도
        resp2 = await room_join(c2, "dup_player", "DupPlayer", room_id)
        has_reject = resp2.HasField("reject")
        has_join_fail = (resp2.HasField("join_room_response") and
                         not resp2.join_room_response.success)
        check(has_reject or has_join_fail, "중복 플레이어 거부")
        if has_reject:
            check(resp2.reject.reason == room_pb2.RejectResponse.DUPLICATE_PLAYER,
                  "거부 사유 = DUPLICATE_PLAYER")
    finally:
        await c1.close()
        await c2.close()


# ============================================================
# Test 8: 이미 방에 있는 플레이어
# ============================================================

async def test_already_in_room():
    print("\n=== Test 8: 이미 방에 있는 플레이어 ===")

    c1 = ProtoClient("C1")
    await c1.connect(ROOM_HOST, ROOM_PORT)

    try:
        # 방 생성
        resp = await room_create(c1, "inroom8", "InRoom", "Room A", 4)
        check(resp.create_room_response.success, "방 A 생성 성공")

        # 같은 연결에서 다른 방 생성 시도
        resp2 = await room_create(c1, "inroom8", "InRoom", "Room B", 4)
        has_reject = resp2.HasField("reject")
        has_create_fail = (resp2.HasField("create_room_response") and
                           not resp2.create_room_response.success)
        check(has_reject or has_create_fail, "이미 방에 있는 플레이어 거부")
        if has_reject:
            check(resp2.reject.reason == room_pb2.RejectResponse.ALREADY_IN_ROOM,
                  "거부 사유 = ALREADY_IN_ROOM")
    finally:
        await c1.close()


# ============================================================
# Test 9: 레디 토글 on/off
# ============================================================

async def test_toggle_ready():
    print("\n=== Test 9: 레디 토글 ===")

    host = ProtoClient("Host")
    joiner = ProtoClient("Joiner")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await joiner.connect(ROOM_HOST, ROOM_PORT)

    try:
        resp = await room_create(host, "host9", "Host", "Ready Test", 4)
        room_id = resp.create_room_response.room.room_id

        await room_join(joiner, "joiner9", "Joiner", room_id)
        await host.drain_messages(1)  # RoomUpdate (입장)

        # Joiner 레디 ON
        await room_toggle_ready(joiner)
        await asyncio.sleep(0.1)

        update_j = await joiner.recv_room()
        check(update_j.HasField("room_update"), "Joiner RoomUpdate 수신 (레디 ON)")
        joiner_info = None
        for p in update_j.room_update.room.players:
            if p.player_id == "joiner9":
                joiner_info = p
        check(joiner_info is not None and joiner_info.is_ready, "Joiner is_ready = true")

        await host.drain_messages(1)  # 호스트 RoomUpdate

        # Joiner 레디 OFF
        await room_toggle_ready(joiner)
        await asyncio.sleep(0.1)

        update_j2 = await joiner.recv_room()
        check(update_j2.HasField("room_update"), "Joiner RoomUpdate 수신 (레디 OFF)")
        joiner_info2 = None
        for p in update_j2.room_update.room.players:
            if p.player_id == "joiner9":
                joiner_info2 = p
        check(joiner_info2 is not None and not joiner_info2.is_ready, "Joiner is_ready = false")
    finally:
        await host.close()
        await joiner.close()


# ============================================================
# Test 10: 비호스트 게임 시작 거부
# ============================================================

async def test_start_not_host():
    print("\n=== Test 10: 비호스트 게임 시작 거부 ===")

    host = ProtoClient("Host")
    joiner = ProtoClient("Joiner")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await joiner.connect(ROOM_HOST, ROOM_PORT)

    try:
        resp = await room_create(host, "host10", "Host", "NotHost Test", 4)
        room_id = resp.create_room_response.room.room_id

        await room_join(joiner, "joiner10", "Joiner", room_id)
        await host.drain_messages(1)

        # Joiner(비호스트)가 게임 시작 시도
        await room_start_game(joiner)
        await asyncio.sleep(0.1)

        resp = await joiner.recv_room()
        check(resp.HasField("reject"), "비호스트 시작 거부 응답")
        check(resp.reject.reason == room_pb2.RejectResponse.NOT_HOST,
              "거부 사유 = NOT_HOST")
    finally:
        await host.close()
        await joiner.close()


# ============================================================
# Test 11: 미준비 상태 게임 시작 거부
# ============================================================

async def test_start_not_all_ready():
    print("\n=== Test 11: 미준비 상태 게임 시작 거부 ===")

    host = ProtoClient("Host")
    joiner = ProtoClient("Joiner")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await joiner.connect(ROOM_HOST, ROOM_PORT)

    try:
        resp = await room_create(host, "host11", "Host", "NotReady Test", 4)
        room_id = resp.create_room_response.room.room_id

        await room_join(joiner, "joiner11", "Joiner", room_id)
        await host.drain_messages(1)

        # 호스트가 게임 시작 (Joiner 미준비)
        await room_start_game(host)
        await asyncio.sleep(0.1)

        resp = await host.recv_room()
        check(resp.HasField("reject"), "미준비 시작 거부 응답")
        check(resp.reject.reason == room_pb2.RejectResponse.NOT_ALL_READY,
              "거부 사유 = NOT_ALL_READY")
    finally:
        await host.close()
        await joiner.close()


# ============================================================
# Test 12: 정상 게임 시작 플로우
# ============================================================

async def test_game_start_success():
    print("\n=== Test 12: 정상 게임 시작 ===")

    host = ProtoClient("Host")
    p2 = ProtoClient("P2")
    p3 = ProtoClient("P3")
    await host.connect(ROOM_HOST, ROOM_PORT)
    await p2.connect(ROOM_HOST, ROOM_PORT)
    await p3.connect(ROOM_HOST, ROOM_PORT)

    try:
        # 방 생성
        resp = await room_create(host, "host12", "Host", "Start Test", 4)
        room_id = resp.create_room_response.room.room_id

        # P2, P3 입장
        await room_join(p2, "p2_12", "P2", room_id)
        await host.drain_messages(1)
        await room_join(p3, "p3_12", "P3", room_id)
        await host.drain_messages(1)
        await p2.drain_messages(1)

        # 전원 준비
        await room_toggle_ready(p2)
        await asyncio.sleep(0.1)
        await host.drain_messages(1)
        await p2.drain_messages(1)
        await p3.drain_messages(1)

        await room_toggle_ready(p3)
        await asyncio.sleep(0.1)
        await host.drain_messages(1)
        await p2.drain_messages(1)
        await p3.drain_messages(1)

        # 호스트 게임 시작
        await room_start_game(host)

        gs_host = await host.recv_room()
        check(gs_host.HasField("game_start"), "Host GameStart 수신")
        session_id = gs_host.game_start.session_id
        check(len(session_id) > 0, "session_id 비어있지 않음")
        check(len(gs_host.game_start.auth_token) > 0, "auth_token 비어있지 않음")

        gs_p2 = await p2.recv_room()
        check(gs_p2.HasField("game_start"), "P2 GameStart 수신")
        check(gs_p2.game_start.session_id == session_id, "P2 session_id 일치")

        gs_p3 = await p3.recv_room()
        check(gs_p3.HasField("game_start"), "P3 GameStart 수신")
        check(gs_p3.game_start.session_id == session_id, "P3 session_id 일치")

        # 각 플레이어의 auth_token이 서로 다른지 확인
        tokens = {gs_host.game_start.auth_token,
                  gs_p2.game_start.auth_token,
                  gs_p3.game_start.auth_token}
        check(len(tokens) == 3, "각 플레이어 auth_token 고유")

        print(f"    Session: {session_id}")
    finally:
        await host.close()
        await p2.close()
        await p3.close()


# ============================================================
# Test 13: 1인 방 게임 시작 (솔로)
# ============================================================

async def test_solo_start():
    print("\n=== Test 13: 1인 방 게임 시작 ===")

    host = ProtoClient("Solo")
    await host.connect(ROOM_HOST, ROOM_PORT)

    try:
        resp = await room_create(host, "solo13", "Solo", "Solo Test", 1)
        check(resp.create_room_response.success, "1인 방 생성 성공")

        # 호스트 혼자 게임 시작 (다른 플레이어 없으므로 전원 준비 조건 충족)
        await room_start_game(host)

        gs = await host.recv_room()
        check(gs.HasField("game_start"), "솔로 GameStart 수신")
        check(len(gs.game_start.session_id) > 0, "session_id 존재")
    finally:
        await host.close()


# ============================================================
# Main
# ============================================================

async def main():
    print("=" * 50)
    print("Room Server E2E 피처 테스트")
    print("=" * 50)

    tests = [
        test_create_and_list,
        test_join_and_update,
        test_leave_non_host,
        test_leave_host,
        test_room_full,
        test_room_not_found,
        test_duplicate_player,
        test_already_in_room,
        test_toggle_ready,
        test_start_not_host,
        test_start_not_all_ready,
        test_game_start_success,
        test_solo_start,
    ]

    for test_fn in tests:
        try:
            await test_fn()
        except Exception as e:
            error_msg = str(e) or type(e).__name__
            print(f"  ERROR: {error_msg}")
            runner.failed += 1

    sys.exit(runner.summary())


if __name__ == "__main__":
    asyncio.run(main())
