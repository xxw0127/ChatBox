#!/usr/bin/env python3
import argparse
import secrets
import struct
import sys
import time

from test_multidevice_auth import (
    encode_string,
    login,
    open_connection,
    read_u64,
    register,
)
from test_multidevice_fanout import (
    CMD_CHAT_PUSH,
    CMD_CHAT_SEND,
    CMD_PRIVATE_PUSH,
    CMD_PRIVATE_SEND,
    DEFAULT_ROOM,
    join_room,
    send,
    wait_for_command,
)

CMD_PRIVATE_MARK_READ = 0x0034
CMD_GROUP_MARK_READ = 0x00BD
CMD_READ_STATE_PUSH = 0x00D2
CMD_MESSAGE_ECHO_PUSH = 0x00D5


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19100)
    args = parser.parse_args()

    password = "TestPass123!"
    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    user_a = f"ra{suffix}"
    user_b = f"rb{suffix}"
    sockets = []
    try:
        register(args.host, args.port, user_a, password)
        register(args.host, args.port, user_b, password)

        a1 = open_connection(args.host, args.port)
        a2 = open_connection(args.host, args.port)
        b1 = open_connection(args.host, args.port)
        sockets.extend([a1, a2, b1])
        a_id, _, _, _, _ = login(a1, 1, user_a, password, "ra-1")
        login(a2, 2, user_a, password, "ra-2")
        b_id, _, _, _, _ = login(b1, 3, user_b, password, "rb-1")
        print("PASS devices logged in")

        # Private message from B; both A devices receive it.
        send(b1, CMD_PRIVATE_SEND, 10,
             struct.pack("!Q", a_id)
             + encode_string(secrets.token_hex(16))
             + encode_string("read/echo private"))
        wait_for_command(a1, CMD_PRIVATE_PUSH, "A device1 private push")
        private_push = wait_for_command(a2, CMD_PRIVATE_PUSH, "A device2 private push")
        message_id, _ = read_u64(private_push, 8)

        # A1 reads it; A2 receives read state.
        send(a1, CMD_PRIVATE_MARK_READ, 11,
             struct.pack("!Q", b_id) + struct.pack("!Q", message_id))
        wait_for_command(a1, CMD_PRIVATE_MARK_READ, "private mark read response")
        read_state = wait_for_command(a2, CMD_READ_STATE_PUSH, "A device2 read state")
        if read_state[0] != 2:
            raise AssertionError(f"read state kind expected 2, got {read_state[0]}")
        target, offset = read_u64(read_state, 1)
        last_read, offset = read_u64(read_state, offset)
        unread = struct.unpack_from("!I", read_state, offset)[0]
        if target != b_id or last_read != message_id or unread != 0:
            raise AssertionError(
                f"unexpected private read state target={target} last={last_read} unread={unread}"
            )
        print("PASS private read state reaches other device")

        # A1 sends private to B; A2 receives the echo.
        send(a1, CMD_PRIVATE_SEND, 12,
             struct.pack("!Q", b_id)
             + encode_string(secrets.token_hex(16))
             + encode_string("echo to A2"))
        wait_for_command(a1, 0x0031, "private send response")
        wait_for_command(b1, CMD_PRIVATE_PUSH, "B device private push")
        echo = wait_for_command(a2, CMD_MESSAGE_ECHO_PUSH, "A device2 private echo")
        if echo[0] != 2:
            raise AssertionError(f"echo kind expected 2, got {echo[0]}")
        echo_target, _ = read_u64(echo, 1)
        if echo_target != b_id:
            raise AssertionError(f"echo target expected {b_id}, got {echo_target}")
        print("PASS private echo reaches sender other device")

        # Group read state reaches the other device.
        join_room(a1, 20)
        join_room(a2, 21)
        send(a1, CMD_CHAT_SEND, 22,
             struct.pack("!I", DEFAULT_ROOM)
             + encode_string(secrets.token_hex(16))
             + encode_string("group read state"))
        wait_for_command(a1, CMD_CHAT_PUSH, "A device1 group push")
        group_push = wait_for_command(a2, CMD_CHAT_PUSH, "A device2 group push")
        group_message_id, _ = read_u64(group_push, 4)
        send(a1, CMD_GROUP_MARK_READ, 23,
             struct.pack("!I", DEFAULT_ROOM) + struct.pack("!Q", group_message_id))
        wait_for_command(a1, CMD_GROUP_MARK_READ, "group mark read response")
        group_read = wait_for_command(a2, CMD_READ_STATE_PUSH, "A device2 group read state")
        if group_read[0] != 1:
            raise AssertionError(f"group read state kind expected 1, got {group_read[0]}")
        print("PASS group read state reaches other device")

        print("ALL READ/ECHO CHECKS PASSED")
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
