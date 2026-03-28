"""
공유 TCP 클라이언트 + 테스트 유틸리티

Room/Chat 서버 E2E 테스트에서 공통으로 사용하는 모듈.
- ProtoClient: 4byte LE 프레이밍 TCP 클라이언트
- TestRunner: 테스트 결과 집계 + check 헬퍼
"""

import asyncio
import struct
import sys

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

    async def try_recv_room(self, timeout: float = 1.0):
        """메시지가 있으면 반환, 없으면 None"""
        try:
            return await self.recv_room(timeout=timeout)
        except asyncio.TimeoutError:
            return None

    async def try_recv_chat(self, timeout: float = 1.0):
        """메시지가 있으면 반환, 없으면 None"""
        try:
            return await self.recv_chat(timeout=timeout)
        except asyncio.TimeoutError:
            return None

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

class TestRunner:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, condition: bool, description: str):
        if condition:
            self.passed += 1
            print(f"    PASS: {description}")
        else:
            self.failed += 1
            print(f"    FAIL: {description}")

    def summary(self) -> int:
        print(f"\n{'=' * 50}")
        print(f"결과: {self.passed} passed, {self.failed} failed")
        print(f"{'=' * 50}")
        return 1 if self.failed > 0 else 0


# ============================================================
# Room 헬퍼
# ============================================================

async def room_create(client: ProtoClient, player_id: str, player_name: str,
                      room_name: str, max_players: int = 4) -> room_pb2.Envelope:
    """방 생성 요청 후 응답 반환"""
    env = room_pb2.Envelope()
    env.create_room.player_id = player_id
    env.create_room.player_name = player_name
    env.create_room.room_name = room_name
    env.create_room.max_players = max_players
    client.send_room(env)
    return await client.recv_room()


async def room_join(client: ProtoClient, player_id: str, player_name: str,
                    room_id: str) -> room_pb2.Envelope:
    """방 입장 요청 후 응답 반환"""
    env = room_pb2.Envelope()
    env.join_room.player_id = player_id
    env.join_room.player_name = player_name
    env.join_room.room_id = room_id
    client.send_room(env)
    return await client.recv_room()


async def room_leave(client: ProtoClient):
    """방 퇴장 요청"""
    env = room_pb2.Envelope()
    env.leave_room.SetInParent()
    client.send_room(env)


async def room_toggle_ready(client: ProtoClient):
    """준비 토글 요청"""
    env = room_pb2.Envelope()
    env.toggle_ready.SetInParent()
    client.send_room(env)


async def room_start_game(client: ProtoClient):
    """게임 시작 요청"""
    env = room_pb2.Envelope()
    env.start_game.SetInParent()
    client.send_room(env)


async def room_list(client: ProtoClient, page: int = 0, page_size: int = 20) -> room_pb2.Envelope:
    """방 목록 요청 후 응답 반환"""
    env = room_pb2.Envelope()
    env.room_list_request.page = page
    env.room_list_request.page_size = page_size
    client.send_room(env)
    return await client.recv_room()


async def chat_auth(client: ProtoClient, player_id: str, player_name: str,
                    session_id: str = "") -> chat_pb2.ChatEnvelope:
    """Chat 인증 요청 후 응답 반환"""
    env = chat_pb2.ChatEnvelope()
    env.auth.player_id = player_id
    env.auth.player_name = player_name
    env.auth.session_id = session_id
    client.send_chat(env)
    return await client.recv_chat()


async def chat_send(client: ProtoClient, channel, content: str,
                    whisper_target: str = ""):
    """Chat 메시지 전송"""
    env = chat_pb2.ChatEnvelope()
    env.send.channel = channel
    env.send.content = content
    if whisper_target:
        env.send.whisper_target = whisper_target
    client.send_chat(env)
