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
    read_string,
    read_u16,
    read_u32,
    read_u64,
    register,
)
from test_multidevice_fanout import (
    CMD_CHAT_SEND,
    CMD_PRIVATE_SEND,
    DEFAULT_ROOM,
    join_room,
    send,
    wait_for_command,
)

CMD_SYNC_PULL = 0x00D3
CMD_SYNC_RESP = 0x00D4
CMD_PRIVATE_SEND_RESP = 0x0031


def skip_file_meta(body: bytes, offset: int):
    present = body[offset]
    offset += 1
    if present == 0:
        return offset
    _, offset = read_u64(body, offset)
    _, offset = read_string(body, offset)
    _, offset = read_u64(body, offset)
    _, offset = read_string(body, offset)
    offset += 1  # thumb
    _, offset = read_u32(body, offset)
    _, offset = read_u32(body, offset)
    _, offset = read_u32(body, offset)
    return offset


def read_sync(body: bytes):
    count, offset = read_u16(body, 0)
    next_index, offset = read_u16(body, offset)
    has_more = body[offset]
    offset += 1
    conversations = []
    for _ in range(count):
        kind = body[offset]
        offset += 1
        target, offset = read_u64(body, offset)
        message_count, offset = read_u16(body, offset)
        has_more_messages = body[offset]
        offset += 1
        next_after, offset = read_u64(body, offset)
        ids = []
        for _ in range(message_count):
            message_id, offset = read_u64(body, offset)
            ids.append(message_id)
            _, offset = read_u64(body, offset)  # sender
            _, offset = read_string(body, offset)  # nickname
            _, offset = read_string(body, offset)  # content
            _, offset = read_string(body, offset)  # created_at
            _, offset = read_u64(body, offset)  # reply_to
            _, offset = read_string(body, offset)  # reply_sender
            _, offset = read_string(body, offset)  # reply_content
            offset += 1  # recalled
        meta_count, offset = read_u16(body, offset)
        for _ in range(meta_count):
            _, offset = read_u16(body, offset)
            offset = skip_file_meta(body, offset)
        conversations.append(
            {
                "kind": kind,
                "target": target,
                "ids": ids,
                "has_more_messages": bool(has_more_messages),
                "next_after": next_after,
            }
        )
    return {
        "count": count,
        "next_index": next_index,
        "has_more": bool(has_more),
        "conversations": conversations,
    }


def sync_pull(sock, seq: int, kind: int, target: int, last_message_id: int):
    body = struct.pack("!HH", 1, 0)
    body += struct.pack("!BQQ", kind, target, last_message_id)
    send(sock, CMD_SYNC_PULL, seq, body)
    response = wait_for_command(sock, CMD_SYNC_RESP, "sync response")
    return read_sync(response)


def sync_pull_multi(sock, seq: int, cursors):
    body = struct.pack("!HH", len(cursors), 0)
    for kind, target, last_message_id in cursors:
        body += struct.pack("!BQQ", kind, target, last_message_id)
    send(sock, CMD_SYNC_PULL, seq, body)
    response = wait_for_command(sock, CMD_SYNC_RESP, "multi sync response")
    return read_sync(response)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19100)
    args = parser.parse_args()

    password = "TestPass123!"
    suffix = f"{int(time.time()) % 100000000}{secrets.token_hex(3)}"
    user_a = f"sa{suffix}"
    user_b = f"sb{suffix}"
    sockets = []
    try:
        a_id = register(args.host, args.port, user_a, password)
        b_id = register(args.host, args.port, user_b, password)

        b = open_connection(args.host, args.port)
        sockets.append(b)
        login(b, 1, user_b, password, "sb-1")
        # A is offline while B writes private messages.
        for index in range(60):
            send(b, CMD_PRIVATE_SEND, 200 + index,
                 struct.pack("!Q", a_id)
                 + encode_string(secrets.token_hex(16))
                 + encode_string(f"offline private {index}"))
            time.sleep(0.12)
            wait_for_command(b, CMD_PRIVATE_SEND_RESP, "private send ack")
        print("PASS B wrote 60 offline private messages")

        a = open_connection(args.host, args.port)
        sockets.append(a)
        login(a, 3, user_a, password, "sa-1")
        first = sync_pull(a, 4, 2, b_id, 0)
        if not first["conversations"] or len(first["conversations"][0]["ids"]) != 50:
            raise AssertionError(f"first private sync expected 50: {first}")
        if not first["conversations"][0]["has_more_messages"]:
            raise AssertionError("first private sync should report more messages")
        cursor = first["conversations"][0]["next_after"]
        second = sync_pull(a, 5, 2, b_id, cursor)
        ids = first["conversations"][0]["ids"] + second["conversations"][0]["ids"]
        if len(ids) != 60:
            raise AssertionError(f"expected 60 private messages, got {len(ids)}")
        if ids != sorted(ids) or len(set(ids)) != len(ids):
            raise AssertionError("private sync order/duplicates invalid")
        print("PASS private sync 50 + 10, ordered and deduplicated")

        # Group messages while A is offline.
        print("INFO joining default room as B")
        join_room(b, 10)
        print("INFO B joined default room")
        for index in range(60):
            send(b, CMD_CHAT_SEND, 300 + index,
                 struct.pack("!I", DEFAULT_ROOM)
                 + encode_string(secrets.token_hex(16))
                 + encode_string(f"offline group {index}"))
            time.sleep(0.12)
            wait_for_command(b, 0x0021, "group send ack")
            if index == 0:
                print("INFO first group send ack received")
        print("PASS B wrote 60 offline group messages")

        first_group = sync_pull(a, 20, 1, DEFAULT_ROOM, 0)
        if len(first_group["conversations"][0]["ids"]) != 50:
            raise AssertionError(f"first group sync expected 50: {first_group}")
        group_cursor = first_group["conversations"][0]["next_after"]
        second_group = sync_pull(a, 21, 1, DEFAULT_ROOM, group_cursor)
        group_ids = first_group["conversations"][0]["ids"] + second_group["conversations"][0]["ids"]
        if len(group_ids) != 60:
            raise AssertionError(f"expected 60 group messages, got {len(group_ids)}")
        if group_ids != sorted(group_ids) or len(set(group_ids)) != len(group_ids):
            raise AssertionError("group sync order/duplicates invalid")
        print("PASS group sync 50 + 10, ordered and deduplicated")

        # An up-to-date cursor must still return a well-formed empty
        # conversation (mandatory trailing file-meta count must be present).
        up_to_date = sync_pull(a, 22, 2, b_id, ids[-1])
        if up_to_date["conversations"][0]["ids"]:
            raise AssertionError(f"up-to-date sync returned messages: {up_to_date}")
        print("PASS up-to-date cursor returns an empty conversation")

        # Mixed batch: one up-to-date cursor, one stale room the user is not in,
        # and one more up-to-date cursor. The stale cursor must not abort the
        # batch, and all three entries must be parseable.
        mixed = sync_pull_multi(a, 23, [
            (2, b_id, ids[-1]),
            (1, 999999, 0),
            (2, b_id, ids[-1]),
        ])
        if mixed["count"] != 3 or len(mixed["conversations"]) != 3:
            raise AssertionError(f"mixed sync entry count invalid: {mixed}")
        if mixed["has_more"] or mixed["next_index"] != 3:
            raise AssertionError(f"mixed sync pagination invalid: {mixed}")
        if mixed["conversations"][1]["ids"]:
            raise AssertionError(f"stale room returned messages: {mixed}")
        print("PASS stale room cursor does not abort the batch")

        print("ALL SYNC CHECKS PASSED")
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
