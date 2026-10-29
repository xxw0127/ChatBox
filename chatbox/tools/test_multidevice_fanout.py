#!/usr/bin/env python3
import argparse
import secrets
import struct
import sys
import time

from test_multidevice_auth import (
    CMD_ERROR,
    CMD_LOGIN,
    CMD_PING,
    FLAG_ERROR,
    FLAG_RESPONSE,
    encode_string,
    login,
    make_frame,
    open_connection,
    read_frame,
    read_u16,
    read_u32,
    read_u64,
    register,
)

CMD_JOIN_ROOM = 0x0011
CMD_CHAT_SEND = 0x0020
CMD_CHAT_PUSH = 0x0022
CMD_PRIVATE_SEND = 0x0030
CMD_PRIVATE_PUSH = 0x0032
CMD_FRIEND_REQUEST = 0x0090
CMD_FRIEND_RESPOND = 0x0091
CMD_FRIEND_REQUEST_PUSH = 0x0095
CMD_FRIEND_ACCEPT_PUSH = 0x0096
CMD_PROFILE_UPDATE = 0x00A1
CMD_PROFILE_UPDATE_PUSH = 0x00A3

DEFAULT_ROOM = 1


def wait_for_command(sock, command: int, label: str, timeout: float = 5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.1, deadline - time.time())
        sock.settimeout(remaining)
        frame = read_frame(sock)
        if frame[3] == CMD_ERROR or (frame[2] & FLAG_ERROR):
            raise AssertionError(f"{label}: got error frame {frame}")
        if frame[3] == command:
            return frame[5]
    raise AssertionError(f"{label}: timed out waiting for command {command:#x}")


def send(sock, command: int, seq: int, body: bytes):
    sock.sendall(make_frame(command, seq, body))


def join_room(sock, seq: int, room_id: int = DEFAULT_ROOM):
    send(sock, CMD_JOIN_ROOM, seq, struct.pack("!I", room_id))
    wait_for_command(sock, CMD_JOIN_ROOM, "join room")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19100)
    args = parser.parse_args()

    password = "TestPass123!"
    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    user_a = f"fa{suffix}"
    user_b = f"fb{suffix}"
    sockets = []
    try:
        register(args.host, args.port, user_a, password)
        register(args.host, args.port, user_b, password)

        a1 = open_connection(args.host, args.port)
        a2 = open_connection(args.host, args.port)
        b1 = open_connection(args.host, args.port)
        b2 = open_connection(args.host, args.port)
        sockets.extend([a1, a2, b1, b2])

        a_id, _, _, _, _ = login(a1, 1, user_a, password, "a-1")
        login(a2, 2, user_a, password, "a-2")
        b_id, _, _, _, _ = login(b1, 3, user_b, password, "b-1")
        login(b2, 4, user_b, password, "b-2")
        print("PASS four devices logged in")

        for index, sock in enumerate(sockets):
            join_room(sock, 10 + index)

        # Private push reaches both devices of the recipient.
        send(b1, CMD_PRIVATE_SEND, 20,
             struct.pack("!Q", a_id)
             + encode_string(secrets.token_hex(16))
             + encode_string("multi-device private"))
        wait_for_command(a1, CMD_PRIVATE_PUSH, "A device1 private push")
        wait_for_command(a2, CMD_PRIVATE_PUSH, "A device2 private push")
        print("PASS private push reaches both A devices")

        # Group push reaches both devices of a member.
        send(b1, CMD_CHAT_SEND, 21,
             struct.pack("!I", DEFAULT_ROOM)
             + encode_string(secrets.token_hex(16))
             + encode_string("multi-device group"))
        wait_for_command(a1, CMD_CHAT_PUSH, "A device1 group push")
        wait_for_command(a2, CMD_CHAT_PUSH, "A device2 group push")
        print("PASS group push reaches both A devices")
        # The sender's own devices must also receive the fan-out so a message
        # typed on the phone shows up on the desktop immediately.
        wait_for_command(b1, CMD_CHAT_PUSH, "B device1 self group push")
        wait_for_command(b2, CMD_CHAT_PUSH, "B device2 self group push")
        print("PASS group push reaches both sender devices")

        # Friend request reaches both A devices.
        send(b1, CMD_FRIEND_REQUEST, 22, struct.pack("!Q", a_id))
        wait_for_command(a1, CMD_FRIEND_REQUEST_PUSH, "A device1 friend request")
        wait_for_command(a2, CMD_FRIEND_REQUEST_PUSH, "A device2 friend request")
        print("PASS friend request reaches both A devices")

        # A accepts; both B devices get the accept push.
        send(a1, CMD_FRIEND_RESPOND, 23,
             struct.pack("!Q", b_id) + bytes([1]))
        wait_for_command(a1, CMD_FRIEND_RESPOND, "friend accept response")
        wait_for_command(b1, CMD_FRIEND_ACCEPT_PUSH, "B device1 friend accept")
        wait_for_command(b2, CMD_FRIEND_ACCEPT_PUSH, "B device2 friend accept")
        print("PASS friend accept reaches both B devices")

        # Profile update reaches both B devices (now friends).
        send(a1, CMD_PROFILE_UPDATE, 24,
             encode_string("A-new-name") + encode_string("") + bytes([0]))
        wait_for_command(a1, CMD_PROFILE_UPDATE, "profile update response")
        wait_for_command(b1, CMD_PROFILE_UPDATE_PUSH, "B device1 profile push")
        wait_for_command(b2, CMD_PROFILE_UPDATE_PUSH, "B device2 profile push")
        print("PASS profile update reaches both B devices")

        # All four devices still respond to PING.
        for index, sock in enumerate(sockets):
            send(sock, CMD_PING, 30 + index, b"")
            frame = read_frame(sock)
            if frame[3] != CMD_PING or not (frame[2] & FLAG_RESPONSE):
                raise AssertionError(f"ping failed on device {index}: {frame}")
        print("PASS all devices still alive")

        print("ALL MULTI-DEVICE FANOUT CHECKS PASSED")
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

