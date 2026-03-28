"""
Chat Server + Room Server 통합 테스트 스크립트

사전 준비:
  1. docker compose up -d redis
  2. room-server.exe 실행 (터미널 1)
  3. chat-server.exe 실행 (터미널 2)
  4. pip install protobuf
  5. python scripts/test_chat.py (터미널 3)
"""

import asyncio
import sys

sys.path.insert(0, "scripts")
import chat_pb2
import room_pb2
from proto_client import (
    ProtoClient, TestRunner,
    room_create, room_join, room_toggle_ready, room_start_game,
    chat_auth, chat_send,
)

runner = TestRunner()
check = runner.check


# ============================================================
# Test 1: 로비 채팅
# ============================================================

async def test_lobby_chat():
    print("\n=== Test 1: 로비 채팅 ===")

    alice = ProtoClient("Alice")
    bob = ProtoClient("Bob")

    await alice.connect("127.0.0.1", 8082)
    await bob.connect("127.0.0.1", 8082)

    # Alice 인증
    resp = await chat_auth(alice, "alice", "Alice")
    check(resp.HasField("auth_result"), "Alice 인증 응답 수신")
    check(resp.auth_result.success, "Alice 인증 성공")

    # Bob 인증
    resp = await chat_auth(bob, "bob", "Bob")
    check(resp.HasField("auth_result"), "Bob 인증 응답 수신")
    check(resp.auth_result.success, "Bob 인증 성공")

    # Alice -> 로비 메시지 전송
    await chat_send(alice, chat_pb2.CHANNEL_LOBBY, "Hello from Alice!")

    await asyncio.sleep(0.1)

    # Alice도 수신해야 함 (로비 브로드캐스트)
    msg_alice = await alice.recv_chat()
    check(msg_alice.HasField("receive"), "Alice 본인 메시지 수신")
    check(msg_alice.receive.sender_name == "Alice", "발신자 이름 = Alice")
    check(msg_alice.receive.content == "Hello from Alice!", "메시지 내용 일치")
    check(msg_alice.receive.channel == chat_pb2.CHANNEL_LOBBY, "채널 = LOBBY")

    # Bob도 수신
    msg_bob = await bob.recv_chat()
    check(msg_bob.HasField("receive"), "Bob 메시지 수신")
    check(msg_bob.receive.content == "Hello from Alice!", "Bob이 받은 내용 일치")

    # Bob -> 로비 메시지
    await chat_send(bob, chat_pb2.CHANNEL_LOBBY, "Hi Alice!")

    await asyncio.sleep(0.1)

    msg = await alice.recv_chat()
    check(msg.receive.sender_name == "Bob", "Alice가 Bob 메시지 수신")

    await alice.close()
    await bob.close()


# ============================================================
# Test 2: 귓속말 (Whisper)
# ============================================================

async def test_whisper():
    print("\n=== Test 2: 귓속말 ===")

    alice = ProtoClient("Alice")
    bob = ProtoClient("Bob")

    await alice.connect("127.0.0.1", 8082)
    await bob.connect("127.0.0.1", 8082)

    # 인증
    await chat_auth(alice, "alice2", "Alice")
    await chat_auth(bob, "bob2", "Bob")

    # Alice -> Bob 귓속말
    await chat_send(alice, chat_pb2.CHANNEL_WHISPER, "Secret message",
                    whisper_target="bob2")

    await asyncio.sleep(0.1)

    # Bob이 수신
    msg_bob = await bob.recv_chat()
    check(msg_bob.HasField("receive"), "Bob 귓속말 수신")
    check(msg_bob.receive.channel == chat_pb2.CHANNEL_WHISPER, "채널 = WHISPER")
    check(msg_bob.receive.content == "Secret message", "귓속말 내용 일치")

    # Alice도 에코 수신
    msg_alice = await alice.recv_chat()
    check(msg_alice.receive.content == "Secret message", "Alice 에코 수신")

    await alice.close()
    await bob.close()


# ============================================================
# Test 3: 메시지 검증
# ============================================================

async def test_message_validation():
    print("\n=== Test 3: 메시지 검증 ===")

    client = ProtoClient("Tester")
    await client.connect("127.0.0.1", 8082)

    # 인증
    await chat_auth(client, "tester", "Tester")

    # 빈 메시지
    await chat_send(client, chat_pb2.CHANNEL_LOBBY, "   ")

    await asyncio.sleep(0.1)
    resp = await client.recv_chat()
    check(resp.HasField("error"), "빈 메시지 -> 에러 응답")
    check(resp.error.code == chat_pb2.ChatError.MESSAGE_TOO_LONG, "에러 코드 = MESSAGE_TOO_LONG")

    # 초과 길이 (200바이트 초과)
    await chat_send(client, chat_pb2.CHANNEL_LOBBY, "A" * 201)

    await asyncio.sleep(0.1)
    resp = await client.recv_chat()
    check(resp.HasField("error"), "초과 길이 -> 에러 응답")

    # 미인증 상태 테스트
    raw = ProtoClient("Raw")
    await raw.connect("127.0.0.1", 8082)

    await chat_send(raw, chat_pb2.CHANNEL_LOBBY, "test")

    await asyncio.sleep(0.1)
    resp = await raw.recv_chat()
    check(resp.HasField("error"), "미인증 전송 -> 에러 응답")
    check(resp.error.code == chat_pb2.ChatError.NOT_AUTHENTICATED, "에러 코드 = NOT_AUTHENTICATED")

    await client.close()
    await raw.close()


# ============================================================
# Test 4: 중복 접속 (Kick)
# ============================================================

async def test_duplicate_kick():
    print("\n=== Test 4: 중복 접속 Kick ===")

    first = ProtoClient("First")
    second = ProtoClient("Second")

    try:
        await first.connect("127.0.0.1", 8082)

        # 첫 번째 연결 인증
        await chat_auth(first, "dupuser", "DupUser")

        # 동일 player_id로 두 번째 연결
        await second.connect("127.0.0.1", 8082)
        resp = await chat_auth(second, "dupuser", "DupUser")

        # 두 번째 연결 인증 성공
        check(resp.auth_result.success, "두 번째 연결 인증 성공")

        # 첫 번째 연결은 kick 메시지 수신
        try:
            kick_msg = await first.recv_chat(timeout=2.0)
            check(kick_msg.HasField("error"), "첫 번째 연결 kick 메시지 수신")
            check(kick_msg.error.code == chat_pb2.ChatError.NOT_AUTHENTICATED,
                  "kick 에러 코드 = NOT_AUTHENTICATED")
        except asyncio.TimeoutError:
            check(False, "첫 번째 연결 kick 메시지 수신 (timeout)")
    finally:
        await first.close()
        await second.close()


# ============================================================
# Test 5: Room + Chat 통합 (세션 채널)
# ============================================================

async def test_room_chat_integration():
    print("\n=== Test 5: Room + Chat 통합 (세션 채널) ===")

    room_alice = ProtoClient("RoomAlice")
    room_bob = ProtoClient("RoomBob")
    chat_alice = ProtoClient("ChatAlice")
    chat_bob = ProtoClient("ChatBob")

    try:
        await room_alice.connect("127.0.0.1", 8080)
        await room_bob.connect("127.0.0.1", 8080)

        # Alice: 방 생성
        resp = await room_create(room_alice, "r_alice", "Alice", "Test Room", 4)
        check(resp.HasField("create_room_response"), "방 생성 응답 수신")
        check(resp.create_room_response.success, "방 생성 성공")
        room_id = resp.create_room_response.room.room_id
        print(f"    방 ID: {room_id}")

        # Bob: 방 입장
        join_resp = await room_join(room_bob, "r_bob", "Bob", room_id)
        check(join_resp.HasField("join_room_response"), "Bob 입장 응답 수신")
        check(join_resp.join_room_response.success, "Bob 입장 성공")

        # Alice: RoomUpdate 수신 (Bob 입장 알림)
        update = await room_alice.recv_room()
        check(update.HasField("room_update"), "Alice RoomUpdate 수신")

        # Bob: 준비
        await room_toggle_ready(room_bob)

        await asyncio.sleep(0.1)
        await room_alice.drain_messages(1, timeout=1.0)
        await room_bob.drain_messages(1, timeout=1.0)

        # Alice (호스트): 게임 시작
        await room_start_game(room_alice)

        # GameStart 메시지 수신
        gs_alice = await room_alice.recv_room()
        check(gs_alice.HasField("game_start"), "Alice GameStart 수신")
        session_id = gs_alice.game_start.session_id
        print(f"    Session ID: {session_id}")

        gs_bob = await room_bob.recv_room()
        check(gs_bob.HasField("game_start"), "Bob GameStart 수신")
        check(gs_bob.game_start.session_id == session_id, "세션 ID 일치")

        # Chat Server에 SessionCreated가 전달될 시간 대기
        await asyncio.sleep(0.5)

        # Chat Server에 세션 채널로 인증
        await chat_alice.connect("127.0.0.1", 8082)
        await chat_bob.connect("127.0.0.1", 8082)

        resp = await chat_auth(chat_alice, "r_alice", "Alice", session_id)
        check(resp.HasField("auth_result"), "Alice 세션 인증 응답")
        check(resp.auth_result.success, "Alice 세션 인증 성공")

        resp = await chat_auth(chat_bob, "r_bob", "Bob", session_id)
        check(resp.HasField("auth_result"), "Bob 세션 인증 응답")
        check(resp.auth_result.success, "Bob 세션 인증 성공")

        # ALL 채널 메시지 전송
        await chat_send(chat_alice, chat_pb2.CHANNEL_ALL, "Team chat works!")

        await asyncio.sleep(0.1)

        msg_alice = await chat_alice.recv_chat()
        check(msg_alice.HasField("receive"), "Alice ALL 메시지 수신")
        check(msg_alice.receive.channel == chat_pb2.CHANNEL_ALL, "채널 = ALL")
        check(msg_alice.receive.content == "Team chat works!", "메시지 내용 일치")

        msg_bob = await chat_bob.recv_chat()
        check(msg_bob.HasField("receive"), "Bob ALL 메시지 수신")
        check(msg_bob.receive.content == "Team chat works!", "Bob이 받은 내용 일치")

        # 로비 채널로는 전송 불가 (세션 모드)
        await chat_send(chat_alice, chat_pb2.CHANNEL_LOBBY, "Should fail")

        await asyncio.sleep(0.1)
        err = await chat_alice.recv_chat()
        check(err.HasField("error"), "세션 모드에서 로비 전송 -> 에러")
        check(err.error.code == chat_pb2.ChatError.CHANNEL_NOT_JOINED,
              "에러 코드 = CHANNEL_NOT_JOINED")
    finally:
        await chat_alice.close()
        await chat_bob.close()
        await room_alice.close()
        await room_bob.close()


# ============================================================
# Main
# ============================================================

async def main():
    print("=" * 50)
    print("Chat Server + Room Server 통합 테스트")
    print("=" * 50)

    tests = [
        test_lobby_chat,
        test_whisper,
        test_message_validation,
        test_duplicate_kick,
        test_room_chat_integration,
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
