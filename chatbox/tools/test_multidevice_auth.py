#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time

MAGIC = 0x4348
VERSION = 1
HEADER = struct.Struct("!HBBHII")

CMD_REGISTER = 0x0001
CMD_LOGIN = 0x0002
CMD_LOGOUT = 0x0003
CMD_RESUME = 0x0004
CMD_PING = 0x0040
CMD_SESSION_LIST = 0x00D0
CMD_SESSION_REVOKE = 0x00D1
CMD_ERROR = 0x00FF

FLAG_RESPONSE = 0x01
FLAG_ERROR = 0x04

CAP_MULTI_DEVICE = 1 << 0
CAP_SYNC_PULL = 1 << 1
CAP_READ_STATE_PUSH = 1 << 2
CAP_MESSAGE_ECHO = 1 << 3
CAP_SESSION_MANAGE = 1 << 4
CAP_DEVICE_METADATA = 1 << 5
ALL_CAPS = (
    CAP_MULTI_DEVICE
    | CAP_SYNC_PULL
    | CAP_READ_STATE_PUSH
    | CAP_MESSAGE_ECHO
    | CAP_SESSION_MANAGE
    | CAP_DEVICE_METADATA
)


def encode_string(value: str) -> bytes:
    data = value.encode("utf-8")
    if len(data) > 0xFFFF:
        raise ValueError("string too long")
    return struct.pack("!H", len(data)) + data


def make_frame(cmd: int, seq: int, body: bytes = b"") -> bytes:
    return HEADER.pack(MAGIC, VERSION, 0, cmd, seq, len(body)) + body


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data.extend(chunk)
    return bytes(data)


def read_frame(sock: socket.socket):
    header = recv_exact(sock, HEADER.size)
    magic, version, flags, cmd, seq, body_len = HEADER.unpack(header)
    return magic, version, flags, cmd, seq, recv_exact(sock, body_len)


def read_string(body: bytes, offset: int):
    if offset + 2 > len(body):
        raise ValueError("truncated string length")
    length = struct.unpack_from("!H", body, offset)[0]
    offset += 2
    if offset + length > len(body):
        raise ValueError("truncated string")
    return body[offset : offset + length].decode("utf-8"), offset + length


def read_u16(body: bytes, offset: int):
    if offset + 2 > len(body):
        raise ValueError("truncated u16")
    return struct.unpack_from("!H", body, offset)[0], offset + 2


def read_u32(body: bytes, offset: int):
    if offset + 4 > len(body):
        raise ValueError("truncated u32")
    return struct.unpack_from("!I", body, offset)[0], offset + 4


def read_u64(body: bytes, offset: int):
    if offset + 8 > len(body):
        raise ValueError("truncated u64")
    return struct.unpack_from("!Q", body, offset)[0], offset + 8


def open_connection(host: str, port: int):
    sock = socket.create_connection((host, port), timeout=5)
    sock.settimeout(5)
    return sock


def expect_command(frame, command: int, label: str):
    if frame[0] != MAGIC or frame[3] != command or frame[2] & FLAG_ERROR:
        raise AssertionError(f"{label}: unexpected frame {frame}")
    return frame[5]


def expect_error(sock: socket.socket, expected_code: int, label: str):
    frame = read_frame(sock)
    if frame[3] != CMD_ERROR or not (frame[2] & FLAG_ERROR):
        raise AssertionError(f"{label}: expected error, got {frame}")
    code = struct.unpack_from("!H", frame[5], 0)[0]
    if code != expected_code:
        raise AssertionError(f"{label}: expected {expected_code}, got {code}")
    print(f"PASS {label} (code {code})")


def ping(sock: socket.socket, seq: int):
    sock.sendall(make_frame(CMD_PING, seq))
    frame = read_frame(sock)
    if frame[3] != CMD_PING or not (frame[2] & FLAG_RESPONSE):
        raise AssertionError(f"ping failed: {frame}")
    print("PASS ping")


def register(host: str, port: int, username: str, password: str) -> int:
    sock = open_connection(host, port)
    body = encode_string(username) + encode_string(password) + encode_string("MultiTester")
    sock.sendall(make_frame(CMD_REGISTER, 1, body))
    body = expect_command(read_frame(sock), CMD_REGISTER, "register")
    user_id, _ = read_u64(body, 0)
    sock.close()
    if user_id == 0:
        raise AssertionError("register returned user_id=0")
    print(f"PASS register {username} -> {user_id}")
    return user_id


def login(sock: socket.socket, seq: int, username: str, password: str,
          device_id: str = "", legacy: bool = False):
    body = encode_string(username) + encode_string(password)
    if not legacy:
        body += struct.pack("!B", 1)
        body += encode_string(device_id)
        body += encode_string("Python device " + device_id)
        body += encode_string("python")
        body += encode_string("test")
        body += struct.pack("!I", ALL_CAPS)
    sock.sendall(make_frame(CMD_LOGIN, seq, body))
    response = expect_command(read_frame(sock), CMD_LOGIN, "login")
    user_id, offset = read_u64(response, 0)
    nickname, offset = read_string(response, offset)
    token, offset = read_string(response, offset)
    server_caps = 0
    ttl = 0
    if len(response) - offset >= 8:
        server_caps, offset = read_u32(response, offset)
        ttl, offset = read_u32(response, offset)
    if len(token) != 64:
        raise AssertionError("login token has wrong length")
    if not legacy:
        if server_caps == 0 or ttl == 0:
            raise AssertionError("login response missing auth extras")
        if server_caps & ALL_CAPS != ALL_CAPS:
            raise AssertionError(f"server caps incomplete: {server_caps:#x}")
    return user_id, nickname, token, server_caps, ttl


def session_list(sock: socket.socket, seq: int):
    sock.sendall(make_frame(CMD_SESSION_LIST, seq))
    body = expect_command(read_frame(sock), CMD_SESSION_LIST, "session list")
    count, offset = read_u16(body, 0)
    sessions = []
    for _ in range(count):
        session_id, offset = read_string(body, offset)
        device_id, offset = read_string(body, offset)
        device_name, offset = read_string(body, offset)
        platform, offset = read_string(body, offset)
        app_version, offset = read_string(body, offset)
        created_at, offset = read_string(body, offset)
        last_seen_at, offset = read_string(body, offset)
        current = body[offset]
        offset += 1
        sessions.append(
            {
                "session_id": session_id,
                "device_id": device_id,
                "device_name": device_name,
                "platform": platform,
                "app_version": app_version,
                "created_at": created_at,
                "last_seen_at": last_seen_at,
                "current": bool(current),
            }
        )
    return sessions


def revoke_session(sock: socket.socket, seq: int, session_id: str):
    sock.sendall(make_frame(CMD_SESSION_REVOKE, seq, encode_string(session_id)))
    expect_command(read_frame(sock), CMD_SESSION_REVOKE, "session revoke")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19100)
    args = parser.parse_args()

    password = "TestPass123!"
    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    username_a = f"ma{suffix}"
    username_b = f"mb{suffix}"
    sockets = []
    try:
        register(args.host, args.port, username_a, password)
        register(args.host, args.port, username_b, password)

        dev1 = open_connection(args.host, args.port)
        sockets.append(dev1)
        _, _, token1, _, _ = login(dev1, 2, username_a, password, "device-1")
        print("PASS device1 login")

        dev2 = open_connection(args.host, args.port)
        sockets.append(dev2)
        _, _, token2, _, _ = login(dev2, 3, username_a, password, "device-2")
        print("PASS device2 login")
        ping(dev1, 4)
        print("PASS device1 still alive after device2 login")

        # Same device re-login replaces only device1.
        dev1b = open_connection(args.host, args.port)
        sockets.append(dev1b)
        _, _, token1b, _, _ = login(dev1b, 5, username_a, password, "device-1")
        expect_error(dev1, 1010, "same-device replacement closes old device1")
        ping(dev2, 6)
        print("PASS device2 still alive after same-device replacement")

        sessions = session_list(dev2, 7)
        if len(sessions) != 2:
            raise AssertionError(f"expected 2 sessions, got {len(sessions)}")
        if not any(item["current"] and item["device_id"] == "device-2" for item in sessions):
            raise AssertionError("device2 is not marked current")
        device1_session = next(
            item["session_id"] for item in sessions if item["device_id"] == "device-1"
        )
        print("PASS session list has device1/device2")

        revoke_session(dev2, 8, device1_session)
        expect_error(dev1b, 1002, "revoked device1 receives 1002")
        ping(dev2, 9)
        print("PASS device2 unaffected by revoking device1")

        # Legacy login must not kick the new device.
        legacy = open_connection(args.host, args.port)
        sockets.append(legacy)
        login(legacy, 10, username_a, password, legacy=True)
        ping(dev2, 11)
        sessions = session_list(dev2, 12)
        if any(item["device_id"] == "device-1" for item in sessions):
            raise AssertionError("revoked device1 still listed")
        if not any(item["device_id"] == "device-2" for item in sessions):
            raise AssertionError("device2 missing after legacy login")
        if not any(item["device_id"] == "" for item in sessions):
            raise AssertionError("legacy session missing after legacy login")
        if any(not item["session_id"] for item in sessions):
            raise AssertionError("session list contains empty session_id")
        print("PASS legacy login does not kick new device")
        print("PASS legacy session has a durable session_id")

        # Cap: six new devices on a fresh account, oldest is evicted with 1011.
        cap_sockets = []
        cap_tokens = []
        for index in range(6):
            sock = open_connection(args.host, args.port)
            sockets.append(sock)
            cap_sockets.append(sock)
            _, _, token, _, _ = login(
                sock, 20 + index, username_b, password, f"cap-{index}"
            )
            cap_tokens.append(token)
            if index < 5:
                ping(sock, 30 + index)
        sessions = session_list(cap_sockets[5], 40)
        if len(sessions) != 5:
            raise AssertionError(f"expected 5 sessions after cap, got {len(sessions)}")
        active_devices = {item["device_id"] for item in sessions}
        evicted = next(
            (index for index in range(5) if f"cap-{index}" not in active_devices),
            None,
        )
        if evicted is None:
            raise AssertionError(f"no cap device was evicted: {active_devices}")
        expect_error(cap_sockets[evicted], 1011, "oldest device evicted at cap")
        print("PASS six devices keep newest five")

        print("ALL MULTI-DEVICE AUTH CHECKS PASSED")
        return 0
    except Exception as exc:  # noqa: BLE001
        print("FAIL:", exc)
        return 1
    finally:
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass


if __name__ == "__main__":
    sys.exit(main())
