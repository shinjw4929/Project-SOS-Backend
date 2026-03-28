"""
부하 테스트 (Python asyncio)

동시 접속, 방 동시 입장, 채팅 폭주 시나리오 벤치마크.

사전 준비:
  1. docker compose up -d redis
  2. room-server.exe 실행
  3. chat-server.exe 실행 (채팅 테스트 시)
  4. pip install protobuf
  5. python scripts/test_load.py [시나리오] [클라이언트수]

사용법:
  python scripts/test_load.py                    # 전체 실행 (기본 10명)
  python scripts/test_load.py connect 50         # 동시 접속 50명
  python scripts/test_load.py room_join 20       # 방 동시 입장 20명
  python scripts/test_load.py chat 30            # 채팅 폭주 30명
  python scripts/test_load.py lifecycle 10       # 전체 생명주기 10명
"""

import asyncio
import sys
import time
from dataclasses import dataclass

sys.path.insert(0, "scripts")
import chat_pb2
import room_pb2
from proto_client import (
    ProtoClient,
    room_create, room_join, room_toggle_ready, room_start_game,
    chat_auth, chat_send,
)

ROOM_HOST = "127.0.0.1"
ROOM_PORT = 8080
CHAT_HOST = "127.0.0.1"
CHAT_PORT = 8082


@dataclass
class BenchResult:
    scenario: str
    clients: int
    success: int
    failed: int
    elapsed_ms: float
    avg_ms: float

    def __str__(self):
        return (f"  {self.scenario}: {self.clients}명 / "
                f"성공 {self.success} / 실패 {self.failed} / "
                f"총 {self.elapsed_ms:.0f}ms / 평균 {self.avg_ms:.1f}ms")


# ============================================================
# Scenario 1: 동시 접속 + 방 생성
# ============================================================

async def bench_connect(n: int) -> BenchResult:
    print(f"\n--- 동시 접속 + 방 생성: {n}명 ---")

    success = 0
    failed = 0

    async def single_client(i: int):
        nonlocal success, failed
        client = ProtoClient(f"C{i}")
        try:
            await client.connect(ROOM_HOST, ROOM_PORT)
            resp = await room_create(client, f"load_p{i}", f"Player{i}",
                                     f"Load Room {i}", 4)
            if resp.create_room_response.success:
                success += 1
            else:
                failed += 1
        except Exception as e:
            failed += 1
            print(f"    C{i} ERROR: {e}")
        finally:
            await client.close()

    start = time.perf_counter()
    await asyncio.gather(*[single_client(i) for i in range(n)])
    elapsed = (time.perf_counter() - start) * 1000

    result = BenchResult("동시 접속+방 생성", n, success, failed,
                         elapsed, elapsed / n if n > 0 else 0)
    print(result)
    return result


# ============================================================
# Scenario 2: 하나의 방에 동시 입장
# ============================================================

async def bench_room_join(n: int) -> BenchResult:
    print(f"\n--- 방 동시 입장: {n}명 ---")

    # 호스트가 방 생성 (max_players = n+1)
    host = ProtoClient("Host")
    await host.connect(ROOM_HOST, ROOM_PORT)
    resp = await room_create(host, "load_host", "Host", "Load Room", min(n + 1, 8))
    room_id = resp.create_room_response.room.room_id
    max_joinable = min(n, 7)  # 최대 8인 - 호스트 1인

    success = 0
    failed = 0
    clients = []

    async def join_client(i: int):
        nonlocal success, failed
        client = ProtoClient(f"J{i}")
        clients.append(client)
        try:
            await client.connect(ROOM_HOST, ROOM_PORT)
            resp = await room_join(client, f"load_j{i}", f"Joiner{i}", room_id)
            if resp.HasField("join_room_response") and resp.join_room_response.success:
                success += 1
            else:
                failed += 1
        except Exception as e:
            failed += 1
            print(f"    J{i} ERROR: {e}")

    start = time.perf_counter()
    await asyncio.gather(*[join_client(i) for i in range(max_joinable)])
    elapsed = (time.perf_counter() - start) * 1000

    result = BenchResult("방 동시 입장", max_joinable, success, failed,
                         elapsed, elapsed / max_joinable if max_joinable > 0 else 0)
    print(result)

    for c in clients:
        await c.close()
    await host.close()
    return result


# ============================================================
# Scenario 3: 채팅 폭주
# ============================================================

async def bench_chat(n: int) -> BenchResult:
    print(f"\n--- 채팅 폭주: {n}명 x 5메시지 ---")

    clients = []
    success = 0
    failed = 0
    messages_per_client = 5

    # 모든 클라이언트 접속 + 인증
    for i in range(n):
        client = ProtoClient(f"Chat{i}")
        try:
            await client.connect(CHAT_HOST, CHAT_PORT)
            resp = await chat_auth(client, f"chat_load_{i}", f"Chatter{i}")
            if resp.auth_result.success:
                clients.append(client)
            else:
                failed += 1
                await client.close()
        except Exception as e:
            failed += 1
            print(f"    Chat{i} 인증 실패: {e}")
            await client.close()

    print(f"  인증 완료: {len(clients)}/{n}명")

    # 동시에 메시지 전송
    send_count = 0
    recv_count = 0

    async def send_messages(client: ProtoClient, idx: int):
        nonlocal send_count
        for j in range(messages_per_client):
            try:
                await chat_send(client, chat_pb2.CHANNEL_LOBBY,
                                f"Load msg {idx}-{j}")
                send_count += 1
                # Rate limit 회피를 위한 약간의 딜레이
                await asyncio.sleep(0.05)
            except Exception:
                pass

    start = time.perf_counter()
    await asyncio.gather(*[send_messages(c, i) for i, c in enumerate(clients)])
    # 메시지 수신 대기
    await asyncio.sleep(1.0)
    elapsed = (time.perf_counter() - start) * 1000

    # 각 클라이언트의 수신 큐 확인
    for c in clients:
        while not c.recv_queue.empty():
            recv_count += 1
            c.recv_queue.get_nowait()

    total_expected = send_count * len(clients)  # 브로드캐스트
    print(f"  전송: {send_count}건 / 수신: {recv_count}건 (예상: ~{total_expected}건)")

    success = send_count
    result = BenchResult("채팅 폭주", len(clients), success, failed,
                         elapsed, elapsed / send_count if send_count > 0 else 0)
    print(result)

    for c in clients:
        await c.close()
    return result


# ============================================================
# Scenario 4: 전체 생명주기 (Room -> GameStart -> Chat -> 종료)
# ============================================================

async def bench_lifecycle(n: int) -> BenchResult:
    print(f"\n--- 전체 생명주기: {n}명 (2인 방 x {n // 2}개) ---")

    pairs = n // 2
    success = 0
    failed = 0

    async def run_pair(pair_idx: int):
        nonlocal success, failed
        host = ProtoClient(f"H{pair_idx}")
        joiner = ProtoClient(f"J{pair_idx}")

        try:
            # Room 접속
            await host.connect(ROOM_HOST, ROOM_PORT)
            await joiner.connect(ROOM_HOST, ROOM_PORT)

            # 방 생성
            resp = await room_create(host, f"lc_h{pair_idx}", f"Host{pair_idx}",
                                     f"LC Room {pair_idx}", 2)
            if not resp.create_room_response.success:
                failed += 1
                return
            room_id = resp.create_room_response.room.room_id

            # 입장
            join_resp = await room_join(joiner, f"lc_j{pair_idx}", f"Joiner{pair_idx}",
                                        room_id)
            if not (join_resp.HasField("join_room_response") and
                    join_resp.join_room_response.success):
                failed += 1
                return
            await host.drain_messages(1)  # RoomUpdate

            # 준비
            await room_toggle_ready(joiner)
            await asyncio.sleep(0.1)
            await host.drain_messages(1)
            await joiner.drain_messages(1)

            # 게임 시작
            await room_start_game(host)
            gs_host = await host.recv_room()
            gs_joiner = await joiner.recv_room()

            if not gs_host.HasField("game_start"):
                failed += 1
                return
            session_id = gs_host.game_start.session_id

            # Chat 접속 + 인증
            await asyncio.sleep(0.3)  # SessionCreated 전파 대기
            chat_h = ProtoClient(f"CH{pair_idx}")
            chat_j = ProtoClient(f"CJ{pair_idx}")
            await chat_h.connect(CHAT_HOST, CHAT_PORT)
            await chat_j.connect(CHAT_HOST, CHAT_PORT)

            auth_h = await chat_auth(chat_h, f"lc_h{pair_idx}", f"Host{pair_idx}",
                                     session_id)
            auth_j = await chat_auth(chat_j, f"lc_j{pair_idx}", f"Joiner{pair_idx}",
                                     session_id)

            if not (auth_h.auth_result.success and auth_j.auth_result.success):
                failed += 1
                await chat_h.close()
                await chat_j.close()
                return

            # ALL 채팅
            await chat_send(chat_h, chat_pb2.CHANNEL_ALL, f"Hello from pair {pair_idx}!")
            await asyncio.sleep(0.1)

            msg = await chat_j.try_recv_chat(timeout=2.0)
            if msg and msg.HasField("receive"):
                success += 1
            else:
                failed += 1

            await chat_h.close()
            await chat_j.close()

        except Exception as e:
            failed += 1
            print(f"    Pair{pair_idx} ERROR: {e}")
        finally:
            await host.close()
            await joiner.close()

    start = time.perf_counter()
    await asyncio.gather(*[run_pair(i) for i in range(pairs)])
    elapsed = (time.perf_counter() - start) * 1000

    result = BenchResult("전체 생명주기", pairs, success, failed,
                         elapsed, elapsed / pairs if pairs > 0 else 0)
    print(result)
    return result


# ============================================================
# Main
# ============================================================

async def main():
    args = sys.argv[1:]
    scenario = args[0] if len(args) >= 1 else "all"
    n = int(args[1]) if len(args) >= 2 else 10

    print("=" * 60)
    print(f"Project-SOS 부하 테스트 (클라이언트: {n}명)")
    print("=" * 60)

    results: list[BenchResult] = []

    if scenario in ("all", "connect"):
        results.append(await bench_connect(n))

    if scenario in ("all", "room_join"):
        results.append(await bench_room_join(n))

    if scenario in ("all", "chat"):
        results.append(await bench_chat(n))

    if scenario in ("all", "lifecycle"):
        results.append(await bench_lifecycle(n))

    # 결과 요약
    print(f"\n{'=' * 60}")
    print("벤치마크 요약")
    print(f"{'=' * 60}")
    print(f"  {'시나리오':<20} {'대상':>6} {'성공':>6} {'실패':>6} {'총(ms)':>10} {'평균(ms)':>10}")
    print(f"  {'-' * 58}")
    for r in results:
        print(f"  {r.scenario:<20} {r.clients:>6} {r.success:>6} {r.failed:>6} "
              f"{r.elapsed_ms:>10.0f} {r.avg_ms:>10.1f}")

    total_failed = sum(r.failed for r in results)
    if total_failed > 0:
        print(f"\n  총 실패: {total_failed}건")
        sys.exit(1)
    else:
        print(f"\n  전체 성공")


if __name__ == "__main__":
    asyncio.run(main())
