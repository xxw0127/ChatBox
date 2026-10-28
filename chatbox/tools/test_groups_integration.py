import os
import socket
import struct
import sys
import uuid

HOST = os.environ.get("CHATBOX_TEST_HOST", "127.0.0.1")
PORT = int(os.environ.get("CHATBOX_TEST_PORT", "19100"))
MAGIC = 0x4348
VERSION = 1

CMD_REGISTER = 0x0001
CMD_LOGIN = 0x0002
CMD_ROOM_LIST = 0x0010
CMD_JOIN_ROOM = 0x0011
CMD_CHAT_SEND = 0x0020
CMD_CHAT_PUSH = 0x0022
CMD_HISTORY = 0x0050
CMD_GROUP_LIST = 0x00B0
CMD_GROUP_CREATE = 0x00B1
CMD_GROUP_DISBAND = 0x00B3
CMD_GROUP_SEARCH = 0x00B4
CMD_GROUP_JOIN_REQUEST = 0x00B5
CMD_GROUP_JOIN_RESPOND = 0x00B6
CMD_GROUP_INVITE = 0x00B7
CMD_GROUP_INVITE_RESPOND = 0x00B8
CMD_GROUP_MEMBER_LIST = 0x00B9
CMD_GROUP_MEMBER_UPDATE = 0x00BA
CMD_GROUP_REQUEST_LIST = 0x00BB
CMD_GROUP_PUSH = 0x00BC
CMD_GROUP_MARK_READ = 0x00BD
CMD_GROUP_LEAVE = 0x00BE


def enc_str(value):
    data = value.encode("utf-8")
    return struct.pack(">H", len(data)) + data


class Reader:
    def __init__(self, data):
        self.data = data
        self.off = 0

    def u8(self):
        value = self.data[self.off]
        self.off += 1
        return value

    def u16(self):
        value = struct.unpack_from(">H", self.data, self.off)[0]
        self.off += 2
        return value

    def u32(self):
        value = struct.unpack_from(">I", self.data, self.off)[0]
        self.off += 4
        return value

    def u64(self):
        value = struct.unpack_from(">Q", self.data, self.off)[0]
        self.off += 8
        return value

    def string(self):
        length = self.u16()
        value = self.data[self.off:self.off + length].decode("utf-8")
        self.off += length
        return value


class Client:
    def __init__(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=10)
        self.seq = 1

    def send(self, cmd, body=b""):
        seq = self.seq
        self.seq += 1
        header = struct.pack(">HBBHII", MAGIC, VERSION, 0, cmd, seq, len(body))
        self.sock.sendall(header + body)
        return seq

    def read_frame(self):
        header = self._read_exact(14)
        magic, version, flags, cmd, seq, length = struct.unpack(">HBBHII", header)
        body = self._read_exact(length) if length else b""
        return flags, cmd, seq, body

    def _read_exact(self, length):
        chunks = []
        remaining = length
        while remaining:
            chunk = self.sock.recv(remaining)
            if not chunk:
                raise RuntimeError("connection closed")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def call(self, cmd, body=b""):
        seq = self.send(cmd, body)
        while True:
            flags, response_cmd, response_seq, response_body = self.read_frame()
            if flags & 0x01 and response_seq == seq:
                if flags & 0x04:
                    reader = Reader(response_body)
                    code = reader.u16()
                    message = reader.string()
                    raise RuntimeError(f"error {code}: {message}")
                return response_cmd, response_body
            # Ignore group pushes and unrelated notifications.

    def group_list(self):
        _, body = self.call(CMD_GROUP_LIST)
        reader = Reader(body)
        count = reader.u16()
        groups = []
        for _ in range(count):
            group = {
                "room_id": reader.u32(),
                "name": reader.string(),
                "group_no": reader.string(),
                "owner_id": reader.u64(),
                "member_count": reader.u32(),
                "role": reader.u8(),
                "unread": reader.u32(),
                "is_default": reader.u8(),
                "status": reader.u8(),
            }
            groups.append(group)
        return groups

    def group_info(self, body):
        reader = Reader(body)
        return {
            "room_id": reader.u32(),
            "name": reader.string(),
            "group_no": reader.string(),
            "owner_id": reader.u64(),
            "member_count": reader.u32(),
            "role": reader.u8(),
            "unread": reader.u32(),
            "is_default": reader.u8(),
            "status": reader.u8(),
        }


def register(client, username, nickname):
    _, body = client.call(CMD_REGISTER, enc_str(username) + enc_str("password123") + enc_str(nickname))
    return Reader(body).u64()


def login(client, username):
    _, body = client.call(CMD_LOGIN, enc_str(username) + enc_str("password123"))
    reader = Reader(body)
    return reader.u64(), reader.string(), reader.string()


def create_group(client, name):
    _, body = client.call(CMD_GROUP_CREATE, enc_str(name))
    return client.group_info(body)


def search_group(client, group_no):
    _, body = client.call(CMD_GROUP_SEARCH, enc_str(group_no))
    reader = Reader(body)
    found = reader.u8()
    if not found:
        return None, 0
    group = {
        "room_id": reader.u32(),
        "name": reader.string(),
        "group_no": reader.string(),
        "owner_id": reader.u64(),
        "member_count": reader.u32(),
        "role": reader.u8(),
        "unread": reader.u32(),
        "is_default": reader.u8(),
        "status": reader.u8(),
    }
    return group, reader.u8()


def requests_for_room(client, room_id):
    _, body = client.call(CMD_GROUP_REQUEST_LIST, struct.pack(">I", room_id))
    reader = Reader(body)
    response_room = reader.u32()
    count = reader.u16()
    items = []
    for _ in range(count):
        items.append({
            "request_id": reader.u64(),
            "room_id": reader.u32(),
            "user_id": reader.u64(),
            "nickname": reader.string(),
            "room_name": reader.string(),
            "message": reader.string(),
            "status": reader.u8(),
            "created_at": reader.string(),
        })
    return response_room, items


def members(client, room_id):
    _, body = client.call(CMD_GROUP_MEMBER_LIST, struct.pack(">I", room_id))
    reader = Reader(body)
    response_room = reader.u32()
    count = reader.u16()
    items = []
    for _ in range(count):
        items.append({
            "user_id": reader.u64(),
            "nickname": reader.string(),
            "role": reader.u8(),
            "online": reader.u8(),
        })
    return response_room, items


def join_room(client, room_id):
    client.call(CMD_JOIN_ROOM, struct.pack(">I", room_id))


def wait_room_push(client, room_id):
    while True:
        flags, cmd, seq, body = client.read_frame()
        if flags & 0x01:
            continue
        if cmd != CMD_CHAT_PUSH:
            continue
        reader = Reader(body)
        pushed_room = reader.u32()
        if pushed_room == room_id:
            return True


def send_room_message(client, room_id, content):
    body = struct.pack(">I", room_id) + enc_str(str(uuid.uuid4())) + enc_str(content)
    _, response = client.call(CMD_CHAT_SEND, body)
    return Reader(response).u64()


def room_list(client):
    _, body = client.call(CMD_ROOM_LIST)
    reader = Reader(body)
    count = reader.u16()
    rooms = []
    for _ in range(count):
        rooms.append({
            "room_id": reader.u32(),
            "name": reader.string(),
            "max_members": reader.u32(),
            "online": reader.u32(),
        })
    return rooms


def room_history_count(client, room_id):
    body = struct.pack(">BQQH", 1, room_id, 0, 50)
    _, response = client.call(CMD_HISTORY, body)
    reader = Reader(response)
    kind = reader.u8()
    target = reader.u64()
    count = reader.u16()
    assert kind == 1 and target == room_id, (kind, target)
    return count


def mark_read(client, room_id, message_id):
    client.call(CMD_GROUP_MARK_READ,
                struct.pack(">IQ", room_id, message_id))


def expect_error(fn, label):
    try:
        fn()
    except RuntimeError as exc:
        print(f"OK error {label}: {exc}")
        return
    raise AssertionError(f"{label} should have failed")


def main():
    a = Client()
    b = Client()
    c = Client()
    a_id = register(a, "group_a", "闃緼")
    b_id = register(b, "group_b", "闃緽")
    c_id = register(c, "group_c", "闃緾")
    login(a, "group_a")
    login(b, "group_b")
    login(c, "group_c")
    print("users", a_id, b_id, c_id)

    anonymous = Client()
    expect_error(lambda: anonymous.call(
        CMD_GROUP_CREATE, enc_str("anonymous")), "unauthenticated create")
    anonymous.sock.close()

    expect_error(lambda: a.call(
        CMD_GROUP_MEMBER_UPDATE,
        struct.pack(">IQB", 1, c_id, 1)), "remove from default")

    groups = a.group_list()
    default = [g for g in groups if g["is_default"]]
    assert len(default) == 1 and default[0]["room_id"] == 1, groups
    assert default[0]["role"] == 2, default
    assert c.group_list()[0]["is_default"] == 1

    legacy_rooms = room_list(c)
    assert len(legacy_rooms) == 1 and legacy_rooms[0]["room_id"] == 1, legacy_rooms

    expect_error(lambda: a.call(CMD_GROUP_DISBAND, struct.pack(">I", 1)), "disband default")
    expect_error(lambda: c.call(CMD_GROUP_LEAVE, struct.pack(">I", 1)), "leave default")

    group = create_group(a, "设计群")
    print("created", group)
    found, membership = search_group(b, group["group_no"])
    assert found and membership == 0, (found, membership)

    _, body = b.call(CMD_GROUP_JOIN_REQUEST,
                     enc_str(group["group_no"]) + enc_str("想加入"))
    request_id = Reader(body).u64()
    _, pending = requests_for_room(a, group["room_id"])
    assert any(item["request_id"] == request_id for item in pending), pending
    a.call(CMD_GROUP_JOIN_RESPOND, struct.pack(">QB", request_id, 1))

    groups = b.group_list()
    assert any(g["room_id"] == group["room_id"] and g["role"] == 0 for g in groups), groups
    _, member_list = members(b, group["room_id"])
    assert {m["user_id"] for m in member_list} == {a_id, b_id}, member_list

    a.call(CMD_GROUP_MEMBER_UPDATE,
           struct.pack(">IQB", group["room_id"], b_id, 2))
    _, member_list = members(a, group["room_id"])
    assert any(m["user_id"] == b_id and m["role"] == 1 for m in member_list), member_list
    expect_error(lambda: b.call(CMD_GROUP_DISBAND, struct.pack(">I", group["room_id"])),
                 "admin disband")

    a.call(CMD_GROUP_MEMBER_UPDATE,
           struct.pack(">IQB", group["room_id"], b_id, 4))
    groups = b.group_list()
    assert any(g["room_id"] == group["room_id"] and g["role"] == 2 for g in groups), groups
    b.call(CMD_GROUP_MEMBER_UPDATE,
           struct.pack(">IQB", group["room_id"], a_id, 1))
    assert all(g["room_id"] != group["room_id"] for g in a.group_list())
    expect_error(lambda: send_room_message(a, group["room_id"], "removed"),
                 "removed member send")
    expect_error(lambda: room_history_count(a, group["room_id"]),
                 "removed member history")
    expect_error(lambda: members(a, group["room_id"]),
                 "removed member list")

    b.call(CMD_GROUP_DISBAND, struct.pack(">I", group["room_id"]))
    dissolved = [g for g in b.group_list() if g["room_id"] == group["room_id"]]
    assert dissolved and dissolved[0]["status"] == 1, dissolved
    expect_error(lambda: send_room_message(b, group["room_id"], "dissolved"),
                 "dissolved send")
    expect_error(lambda: room_history_count(b, group["room_id"]),
                 "dissolved history")
    expect_error(lambda: members(b, group["room_id"]),
                 "dissolved member list")

    group2 = create_group(a, "第二个群")
    _, body = a.call(CMD_GROUP_INVITE,
                     struct.pack(">IQ", group2["room_id"], b_id))
    invite_id = Reader(body).u64()
    _, invites = requests_for_room(b, 0)
    assert any(i["request_id"] == invite_id for i in invites), invites
    b.call(CMD_GROUP_INVITE_RESPOND, struct.pack(">QB", invite_id, 1))
    assert any(g["room_id"] == group2["room_id"] for g in b.group_list())

    message_id = send_room_message(a, group2["room_id"], "加入前的消息")
    assert message_id > 0
    assert room_history_count(b, group2["room_id"]) == 1

    _, body = a.call(CMD_GROUP_INVITE,
                     struct.pack(">IQ", group2["room_id"], c_id))
    c_invite_id = Reader(body).u64()
    _, c_invites = requests_for_room(c, 0)
    assert any(i["request_id"] == c_invite_id for i in c_invites), c_invites
    c.call(CMD_GROUP_INVITE_RESPOND, struct.pack(">QB", c_invite_id, 1))
    assert room_history_count(c, group2["room_id"]) == 0
    assert room_history_count(b, group2["room_id"]) == 1

    mark_read(b, group2["room_id"], 1 << 40)
    send_room_message(b, group2["room_id"], "B自己的消息")
    b_view = [g for g in b.group_list() if g["room_id"] == group2["room_id"]][0]
    a_view = [g for g in a.group_list() if g["room_id"] == group2["room_id"]][0]
    assert b_view["unread"] == 0, b_view
    assert a_view["unread"] >= 1, a_view

    # A pending join request must not survive membership granted by invite,
    # and removal must revoke history/member-list access. Rejoining starts a
    # fresh history cursor.
    group3 = create_group(a, "第三个群")
    _, body = c.call(CMD_GROUP_JOIN_REQUEST,
                     enc_str(group3["group_no"]) + enc_str("申请"))
    stale_request = Reader(body).u64()
    _, body = a.call(CMD_GROUP_INVITE,
                     struct.pack(">IQ", group3["room_id"], c_id))
    c_invite2 = Reader(body).u64()
    c.call(CMD_GROUP_INVITE_RESPOND, struct.pack(">QB", c_invite2, 1))
    expect_error(lambda: a.call(CMD_GROUP_JOIN_RESPOND,
                                struct.pack(">QB", stale_request, 1)),
                 "stale join approval")
    assert any(m["user_id"] == c_id for m in members(a, group3["room_id"])[1])
    a.call(CMD_GROUP_MEMBER_UPDATE,
           struct.pack(">IQB", group3["room_id"], c_id, 1))
    expect_error(lambda: room_history_count(c, group3["room_id"]),
                 "removed member history")
    expect_error(lambda: members(c, group3["room_id"]),
                 "removed member list")
    send_room_message(a, group3["room_id"], "移除后的消息")
    _, body = a.call(CMD_GROUP_INVITE,
                     struct.pack(">IQ", group3["room_id"], c_id))
    c_invite3 = Reader(body).u64()
    c.call(CMD_GROUP_INVITE_RESPOND, struct.pack(">QB", c_invite3, 1))
    assert room_history_count(c, group3["room_id"]) == 0

    utf8_name = "群" * 32
    utf8_group = create_group(a, utf8_name)
    assert utf8_group["name"] == utf8_name, utf8_group
    a.call(CMD_GROUP_DISBAND, struct.pack(">I", utf8_group["room_id"]))

    a.call(CMD_GROUP_MEMBER_UPDATE,
           struct.pack(">IQB", group2["room_id"], b_id, 4))
    a.call(CMD_GROUP_LEAVE, struct.pack(">I", group2["room_id"]))
    assert all(g["room_id"] != group2["room_id"] for g in a.group_list())

    print("ALL GROUP INTEGRATION CHECKS PASSED")
    a.sock.close()
    b.sock.close()
    c.sock.close()


if __name__ == "__main__":
    main()
