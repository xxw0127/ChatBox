#!/usr/bin/env python3
import argparse
import secrets
import socket
import struct
import sys
import time

from test_multidevice_auth import (
    CMD_ERROR,
    encode_string,
    expect_error,
    login,
    open_connection,
    read_frame,
    read_u64,
    register,
)
from test_multidevice_fanout import (
    CMD_PRIVATE_PUSH,
    CMD_PRIVATE_SEND,
    send,
    wait_for_command,
)

CMD_PRIVATE_MARK_READ = 0x0034
CMD_SESSION_LIST = 0x00D0
CMD_SYNC_PULL = 0x00D3
CMD_MESSAGE_ECHO_PUSH = 0x00D5
CMD_READ_STATE_PUSH = 0x00D2


def expect_no_command(sock, command: int, label: str, timeout: float = 1.5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        sock.settimeout(max(0.1, deadline - time.time()))
        try:
            frame = read_frame(sock)
        except (socket.timeout, TimeoutError):
            return
        except ConnectionError:
            return
        if frame[3] == command:
            raise AssertionError(f"{label}: unexpected command {command:#x}")
        if frame[3] == CMD_ERROR:
            raise AssertionError(f"{label}: unexpected error frame {frame}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19100)
    args = parser.parse_args()

    password = "TestPass123!"
    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    user_a = f"ca{suffix}"
    user_b = f"cb{suffix}"
    sockets = []
    try:
        a_id = register(args.host, args.port, user_a, password)
        b_id = register(args.host, args.port, user_b, password)

        a_new = open_connection(args.host, args.port)
        a_legacy = open_connection(args.host, args.port)
        b = open_connection(args.host, args.port)
        sockets.extend([a_new, a_legacy, b])
        login(a_new, 1, user_a, password, "ca-new")
        login(a_legacy, 2, user_a, password, legacy=True)
        login(b, 3, user_b, password, "cb-1")
        print("PASS new + legacy devices logged in")

        send(b, CMD_PRIVATE_SEND, 4,
             struct.pack("!Q", a_id)
             + encode_string(secrets.token_hex(16))
             + encode_string("legacy still gets private push"))
        wait_for_command(a_new, CMD_PRIVATE_PUSH, "new private push")
        legacy_push = wait_for_command(a_legacy, CMD_PRIVATE_PUSH, "legacy private push")
        message_id, _ = read_u64(legacy_push, 8)
        print("PASS legacy device still receives original private push")

        # New client sends to B; legacy device (caps=0) must not get echo.
        send(a_new, CMD_PRIVATE_SEND, 5,
             struct.pack("!Q", b_id)
             + encode_string(secrets.token_hex(16))
             + encode_string("no echo to legacy"))
        wait_for_command(a_new, 0x0031, "send ack")
        wait_for_command(b, CMD_PRIVATE_PUSH, "B private push")
        expect_no_command(a_legacy, CMD_MESSAGE_ECHO_PUSH, "legacy echo")
        print("PASS legacy device does not receive private echo")

        # New client marks read; legacy device must not get read state.
        send(a_new, CMD_PRIVATE_MARK_READ, 6,
             struct.pack("!Q", b_id) + struct.pack("!Q", message_id))
        wait_for_command(a_new, CMD_PRIVATE_MARK_READ, "mark read ack")
        expect_no_command(a_legacy, CMD_READ_STATE_PUSH, "legacy read state")
        print("PASS legacy device does not receive read state")

        # Legacy device cannot call session management.
        send(a_legacy, CMD_SESSION_LIST, 7, b"")
        expect_error(a_legacy, 1001, "legacy session list")
        print("PASS legacy device rejected session list")

        # Legacy device cannot call sync pull.
        send(a_legacy, CMD_SYNC_PULL, 8, struct.pack("!HH", 0, 0))
        expect_error(a_legacy, 1001, "legacy sync pull")
        print("PASS legacy device rejected sync pull")

        # New device can list sessions.
        send(a_new, CMD_SESSION_LIST, 9, b"")
        wait_for_command(a_new, CMD_SESSION_LIST, "new session list")
        print("PASS new device can list sessions")

        print("ALL CAPABILITY CHECKS PASSED")
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
