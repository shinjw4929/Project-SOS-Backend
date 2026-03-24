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
import struct
import sys
import time

# Proto 파일 경로
sys.path.insert(0, "scripts")
import chat_pb2
import room_pb2


# ============================================================
# TCP 클라이언트 (4byte LE 프레이밍)
# ============================================================

class ProtoClient:
    def __init__(self, name: str):
        self.name = name
        self.reader = None
        self.writer = None
        self.recv_queue: asyncio.Queue = asyncio.Queue()
        self._read_task = None

    async def connect(self, host: str, port: int):
        self.reader, self.writer = await asyncio.open_connection(host, port)
        self._read_task = asyncio.create_task(self._read_loop())
        print(f"  [{self.name}] Connected to {host}:{port}")

    async def close(self):
        if self._read_task:
            self._read_task.cancel()
            try:
                await self._read_task
            except asyncio.CancelledError:
                pass
        if self.writer:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass

    def send_chat(self, envelope: chat_pb2.ChatEnvelope):
        data = envelope.SerializeToString()
        header = struct.pack("<I", len(data))
        self.writer.write(header + data)

    def send_room(self, envelope: room_pb2.Envelope):
        data = envelope.SerializeToString()
        header = struct.pack("<I", len(data))
        self.writer.write(header + data)

    async def recv_chat(self, timeout: float = 3.0) -> chat_pb2.ChatEnvelope:
        msg = await asyncio.wait_for(self.recv_queue.get(), timeout=timeout)
        env = chat_pb2.ChatEnvelope()
        env.ParseFromString(msg)
        return env

    async def recv_room(self, timeout: float = 3.0) -> room_pb2.Envelope:
        msg = await asyncio.wait_for(self.recv_queue.get(), timeout=timeout)
        env = room_pb2.Envelope()
        env.ParseFromString(msg)
        return env

    async def drain_messages(self, count: int, timeout: float = 3.0):
        """큐에서 count개 메시지를 꺼내 버린다"""
        for _ in range(count):
            try:
                await asyncio.wait_for(self.recv_queue.get(), timeout=timeout)
            except asyncio.TimeoutError:
                break

    async def _read_loop(self):
        try:
            while True:
                header = await self.reader.readexactly(4)
                length = struct.unpack("<I", header)[0]
                data = await self.reader.readexactly(length)
                await self.recv_queue.put(data)
        except (asyncio.IncompleteReadError, ConnectionResetError, asyncio.CancelledError):
            pass


# ============================================================
# 테스트 유틸
# ============================================================

passed = 0
failed = 0


def check(condition: bool, description: str):
    global passed, failed
    if condition:
        passed += 1
        print(f"    PASS: {description}")
    else:
        failed += 1
        print(f"    FAIL: {description}")


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
    env = chat_pb2.ChatEnvelope()
    env.auth.player_id = "alice"
    env.auth.player_name = "Alice"
    env.auth.session_id = ""
    alice.send_chat(env)

    resp = await alice.recv_chat()
    check(resp.HasField("auth_result"), "Alice 인증 응답 수신")
    check(resp.auth_result.success, "Alice 인증 성공")

    # Bob 인증
    env = chat_pb2.ChatEnvelope()
    env.auth.player_id = "bob"
    env.auth.player_name = "Bob"
    env.auth.session_id = ""
    bob.send_chat(env)

    resp = await bob.recv_chat()
    check(resp.HasField("auth_result"), "Bob 인증 응답 수신")
    check(resp.auth_result.success, "Bob 인증 성공")

    # Alice -> 로비 메시지 전송
    env = chat_pb2.ChatEnvelope()
    env.send.channel = chat_pb2.CHANNEL_LOBBY
    env.send.content = "Hello from Alice!"
    alice.send_chat(env)

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
    env = chat_pb2.ChatEnvelope()
    env.send.channel = chat_pb2.CHANNEL_LOBBY
    env.send.content = "Hi Alice!"
    bob.send_chat(env)

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
    for client, pid, pname in [(alice, "alice2", "Alice"), (bob, "bob2", "Bob")]:
        env = chat_pb2.ChatEnvelope()
        env.auth.player_id = pid
        env.auth.player_name = pname
        env.auth.session_id = ""
        client.send_chat(env)
        await client.recv_chat()

    # Alice -> Bob 귓속말
    env = chat_pb2.ChatEnvelope()
    env.send.channel = chat_pb2.CHANNEL_WHISPER
    env.send.content = "Secret message"
    env.send.whisper_target = "bob2"
    alice.send_chat(env)

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
    env = chat_pb2.ChatEnvelope()
    env.auth.player_id = "tester"
    env.auth.player_name = "Tester"
    env.auth.session_id = ""
    client.send_chat(env)
    await client.recv_chat()

    # 빈 메시지
    env = chat_pb2.ChatEnvelope()
    env.send.channel = chat_pb2.CHANNEL_LOBBY
    env.send.content = "   "
    client.send_chat(env)

    await asyncio.sleep(0.1)
    resp = await client.recv_chat()
    check(resp.HasField("error"), "빈 메시지 -> 에러 응답")
    check(resp.error.code == chat_pb2.ChatError.MESSAGE_TOO_LONG, "에러 코드 = MESSAGE_TOO_LONG")

    # 초과 길이 (200바이트 초과)
    env = chat_pb2.ChatEnvelope()
    env.send.channel = chat_pb2.CHANNEL_LOBBY
    env.send.content = "A" * 201
    client.send_chat(env)

    await asyncio.sleep(0.1)
    resp = await client.recv_chat()
    check(resp.HasField("error"), "초과 길이 -> 에러 응답")

    # 미인증 상태 테스트
    raw = ProtoClient("Raw")
    await raw.connect("127.0.0.1", 8082)

    env = chat_pb2.ChatEnvelope()
    env.send.channel = chat_pb2.CHANNEL_LOBBY
    env.send.content = "test"
    raw.send_chat(env)

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
        env = chat_pb2.ChatEnvelope()
        env.auth.player_id = "dupuser"
        env.auth.player_name = "DupUser"
        env.auth.session_id = ""
        first.send_chat(env)
        await first.recv_chat()

        # 동일 player_id로 두 번째 연결
        await second.connect("127.0.0.1", 8082)
        env = chat_pb2.ChatEnvelope()
        env.auth.player_id = "dupuser"
        env.auth.player_name = "DupUser"
        env.auth.session_id = ""
        second.send_chat(env)

        # 두 번째 연결 인증 성공
        resp = await second.recv_chat()
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

    # Room Server 클라이언트
    room_alice = ProtoClient("RoomAlice")
    room_bob = ProtoClient("RoomBob")
    chat_alice = ProtoClient("ChatAlice")
    chat_bob = ProtoClient("ChatBob")

    try:
        await room_alice.connect("127.0.0.1", 8080)
        await room_bob.connect("127.0.0.1", 8080)

        # Alice: 방 생성
        env = room_pb2.Envelope()
        env.create_room.player_id = "r_alice"
        env.create_room.player_name = "Alice"
        env.create_room.room_name = "Test Room"
        env.create_room.max_players = 4
        room_alice.send_room(env)

        resp = await room_alice.recv_room()
        check(resp.HasField("create_room_response"), "방 생성 응답 수신")
        check(resp.create_room_response.success, "방 생성 성공")
        room_id = resp.create_room_response.room.room_id
        print(f"    방 ID: {room_id}")

        # Bob: 방 입장
        env = room_pb2.Envelope()
        env.join_room.player_id = "r_bob"
        env.join_room.player_name = "Bob"
        env.join_room.room_id = room_id
        room_bob.send_room(env)

        resp = await room_bob.recv_room()
        check(resp.HasField("join_room_response"), "Bob 입장 응답 수신")
        check(resp.join_room_response.success, "Bob 입장 성공")

        # Alice: RoomUpdate 수신 (Bob 입장 알림)
        update = await room_alice.recv_room()
        check(update.HasField("room_update"), "Alice RoomUpdate 수신")

        # Bob: 준비
        env = room_pb2.Envelope()
        env.toggle_ready.SetInParent()
        room_bob.send_room(env)

        await asyncio.sleep(0.1)
        # 양쪽 RoomUpdate 수신
        await room_alice.drain_messages(1, timeout=1.0)
        await room_bob.drain_messages(1, timeout=1.0)

        # Alice (호스트): 게임 시작
        env = room_pb2.Envelope()
        env.start_game.SetInParent()
        room_alice.send_room(env)

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

        # Alice: 세션 인증
        env = chat_pb2.ChatEnvelope()
        env.auth.player_id = "r_alice"
        env.auth.player_name = "Alice"
        env.auth.session_id = session_id
        chat_alice.send_chat(env)

        resp = await chat_alice.recv_chat()
        check(resp.HasField("auth_result"), "Alice 세션 인증 응답")
        check(resp.auth_result.success, "Alice 세션 인증 성공")

        # Bob: 세션 인증
        env = chat_pb2.ChatEnvelope()
        env.auth.player_id = "r_bob"
        env.auth.player_name = "Bob"
        env.auth.session_id = session_id
        chat_bob.send_chat(env)

        resp = await chat_bob.recv_chat()
        check(resp.HasField("auth_result"), "Bob 세션 인증 응답")
        check(resp.auth_result.success, "Bob 세션 인증 성공")

        # ALL 채널 메시지 전송
        env = chat_pb2.ChatEnvelope()
        env.send.channel = chat_pb2.CHANNEL_ALL
        env.send.content = "Team chat works!"
        chat_alice.send_chat(env)

        await asyncio.sleep(0.1)

        msg_alice = await chat_alice.recv_chat()
        check(msg_alice.HasField("receive"), "Alice ALL 메시지 수신")
        check(msg_alice.receive.channel == chat_pb2.CHANNEL_ALL, "채널 = ALL")
        check(msg_alice.receive.content == "Team chat works!", "메시지 내용 일치")

        msg_bob = await chat_bob.recv_chat()
        check(msg_bob.HasField("receive"), "Bob ALL 메시지 수신")
        check(msg_bob.receive.content == "Team chat works!", "Bob이 받은 내용 일치")

        # 로비 채널로는 전송 불가 (세션 모드)
        env = chat_pb2.ChatEnvelope()
        env.send.channel = chat_pb2.CHANNEL_LOBBY
        env.send.content = "Should fail"
        chat_alice.send_chat(env)

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
    global passed, failed

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
            failed += 1

    print("\n" + "=" * 50)
    print(f"결과: {passed} passed, {failed} failed")
    print("=" * 50)

    if failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
