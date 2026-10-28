#!/usr/bin/env python3
"""Phase 2 file-transfer acceptance.

Runs against a disposable ChatBox test database/server. It registers two users,
uploads/downloads, resumes an interrupted upload, checks thumbnails, permissions,
delete propagation, group disband cleanup, and (optionally) a 2GB transfer.

Examples:
  python3 tools/test_files_acceptance.py --host 127.0.0.1 --port 19100 \
      --files-dir /home/ubuntu/chatbox/files
  python3 tools/test_files_acceptance.py ... --large
"""

import argparse
import hashlib
import os
import socket
import ssl
import struct
import time

MAGIC = 0x4348
VERSION = 1
FLAG_RESPONSE = 0x01

CMD_REGISTER = 0x0001
CMD_LOGIN = 0x0002
CMD_HISTORY = 0x0050
CMD_GROUP_CREATE = 0x00B1
CMD_GROUP_DISBAND = 0x00B3
CMD_FILE_AUTH = 0x00C0
CMD_FILE_UPLOAD_INIT = 0x00C1
CMD_FILE_UPLOAD_CHUNK = 0x00C2
CMD_FILE_UPLOAD_STATUS = 0x00C3
CMD_FILE_UPLOAD_COMMIT = 0x00C4
CMD_FILE_UPLOAD_CANCEL = 0x00C5
CMD_FILE_OPEN = 0x00C6
CMD_FILE_READ = 0x00C7
CMD_FILE_CHUNK = 0x00C8
CMD_FILE_READ_ACK = 0x00C9
CMD_FILE_DELETE = 0x00CA
CMD_FILE_INFO = 0x00CB
CMD_FILE_THUMB = 0x00CC
CMD_FILE_LIST = 0x00CD

CHUNK = 60 * 1024
MAX_FILE = 2 * 1024 * 1024 * 1024


def enc_str(value):
    data = value.encode("utf-8") if isinstance(value, str) else value
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
        value = self.data[self.off:self.off + length]
        self.off += length
        return value

    def text(self):
        return self.string().decode("utf-8")


class Client:
    def __init__(self, args, proxy=False):
        raw = socket.create_connection((args.host, args.port), timeout=20)
        if args.tls:
            context = ssl.create_default_context(cafile=args.ca)
            raw = context.wrap_socket(raw, server_hostname=args.host)
        self.sock = raw
        self.seq = 1
        if proxy:
            self.sock.sendall(
                b"PROXY TCP4 127.0.0.1 127.0.0.1 45678 19100\r\n")

    def send(self, cmd, body=b""):
        seq = self.seq
        self.seq += 1
        self.sock.sendall(
            struct.pack(">HBBHII", MAGIC, VERSION, 0, cmd, seq, len(body)) +
            body)
        return seq

    def read_frame(self):
        header = self._read_exact(14)
        magic, version, flags, cmd, seq, length = struct.unpack(
            ">HBBHII", header)
        assert magic == MAGIC and version == VERSION
        body = self._read_exact(length) if length else b""
        return flags, cmd, seq, body

    def _read_exact(self, length):
        chunks = []
        while length:
            chunk = self.sock.recv(length)
            if not chunk:
                raise RuntimeError("connection closed")
            chunks.append(chunk)
            length -= len(chunk)
        return b"".join(chunks)

    def call(self, cmd, body=b""):
        seq = self.send(cmd, body)
        while True:
            flags, out_cmd, out_seq, out_body = self.read_frame()
            if flags & FLAG_RESPONSE and out_seq == seq:
                if flags & 0x04:
                    reader = Reader(out_body)
                    raise RuntimeError(
                        "server error %d: %s" %
                        (reader.u16(), reader.text()))
                return out_cmd, out_body

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def register(client, username, nickname):
    _, body = client.call(
        CMD_REGISTER,
        enc_str(username) + enc_str("password123") + enc_str(nickname))
    return Reader(body).u64()


def login(client, username):
    _, body = client.call(
        CMD_LOGIN, enc_str(username) + enc_str("password123"))
    reader = Reader(body)
    user_id = reader.u64()
    reader.text()
    token = reader.text()
    assert len(token) == 64, token
    return user_id, token


def file_auth(client, token):
    _, body = client.call(CMD_FILE_AUTH, enc_str(token))
    return Reader(body).u64()


def list_files(client, kind, target_id):
    _, body = client.call(
        CMD_FILE_LIST, struct.pack(">BQQH", kind, target_id, 0, 100))
    reader = Reader(body)
    total = reader.u32()
    used = reader.u64()
    count = reader.u16()
    entries = []
    for _ in range(count):
        entries.append({
            "file_id": reader.u64(),
            "uploader_id": reader.u64(),
            "uploader_name": reader.text(),
            "message_id": reader.u64(),
            "name": reader.text(),
            "size": reader.u64(),
            "mime": reader.text(),
            "sha256": reader.text(),
            "thumb": reader.u8(),
            "width": reader.u32(),
            "height": reader.u32(),
            "duration": reader.u32(),
            "created_at": reader.text(),
            "can_delete": reader.u8(),
        })
    return total, used, entries



def file_info(client, file_id):
    _, body = client.call(CMD_FILE_INFO, struct.pack(">Q", file_id))
    reader = Reader(body)
    entry = {
        "file_id": reader.u64(),
        "uploader_id": reader.u64(),
        "uploader_name": reader.text(),
        "message_id": reader.u64(),
        "name": reader.text(),
        "size": reader.u64(),
        "mime": reader.text(),
        "sha256": reader.text(),
        "thumb": reader.u8(),
        "width": reader.u32(),
        "height": reader.u32(),
        "duration": reader.u32(),
        "created_at": reader.text(),
        "can_delete": reader.u8(),
    }
    return entry

def upload_init(client, kind, target_id, name, size, mime, sha256=""):
    body = (struct.pack(">BQ", kind, target_id) + enc_str(name) +
            struct.pack(">Q", size) + enc_str(mime) + enc_str(sha256))
    _, response = client.call(CMD_FILE_UPLOAD_INIT, body)
    reader = Reader(response)
    upload_id = reader.text()
    offset = reader.u64()
    return upload_id, offset


def upload_chunk(client, upload_id, offset, data):
    body = enc_str(upload_id) + struct.pack(">Q", offset) + enc_str(data)
    _, response = client.call(CMD_FILE_UPLOAD_CHUNK, body)
    return Reader(response).u64()


def upload_status(client, upload_id):
    _, response = client.call(CMD_FILE_UPLOAD_STATUS, enc_str(upload_id))
    reader = Reader(response)
    return reader.u64(), reader.u8()


def upload_commit(client, upload_id):
    _, response = client.call(CMD_FILE_UPLOAD_COMMIT, enc_str(upload_id))
    reader = Reader(response)
    return reader.u64(), reader.u64(), reader.text()


def upload_file(client, kind, target_id, name, data, mime):
    digest = hashlib.sha256(data).hexdigest()
    upload_id, offset = upload_init(
        client, kind, target_id, name, len(data), mime, digest)
    assert offset == 0
    while offset < len(data):
        offset = upload_chunk(
            client, upload_id, offset, data[offset:offset + CHUNK])
    message_id, file_id, _ = upload_commit(client, upload_id)
    return file_id, message_id, upload_id


def download_file(client, file_id, expected_sha):
    _, response = client.call(CMD_FILE_OPEN, struct.pack(">Q", file_id))
    reader = Reader(response)
    total = reader.u64()
    token = reader.text()
    data = bytearray()
    while len(data) < total:
        want = min(CHUNK, total - len(data))
        _, chunk_body = client.call(
            CMD_FILE_READ,
            enc_str(token) + struct.pack(">QI", len(data), want))
        chunk_reader = Reader(chunk_body)
        returned_id = chunk_reader.u64()
        offset = chunk_reader.u64()
        chunk = chunk_reader.string()
        assert returned_id == file_id and offset == len(data)
        data.extend(chunk)
        client.send(CMD_FILE_READ_ACK, b"")
    assert hashlib.sha256(bytes(data)).hexdigest() == expected_sha
    return bytes(data)


def history(client, target_id=1):
    _, body = client.call(
        CMD_HISTORY, struct.pack(">BQQH", 1, target_id, 0, 50))
    reader = Reader(body)
    kind = reader.u8()
    target = reader.u64()
    count = reader.u16()
    reader.u8()
    reader.u64()
    messages = []
    for _ in range(count):
        messages.append({
            "id": reader.u64(),
            "sender_id": reader.u64(),
            "nickname": reader.text(),
            "content": reader.text(),
            "created_at": reader.text(),
            "reply_to_id": reader.u64(),
            "reply_sender": reader.text(),
            "reply_content": reader.text(),
            "recalled": reader.u8(),
        })
    # 0.2.5-compatible parsers stop at this point and ignore the appended file
    # metadata table, so leaving it unread is intentional.
    assert kind == 1 and target == target_id
    return messages


def make_png(width=64, height=48):
    import binascii
    import zlib

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", binascii.crc32(tag + payload) & 0xFFFFFFFF))

    raw = b""
    for y in range(height):
        raw += b"\x00"
        for x in range(width):
            raw += bytes([(x * 4) % 256, (y * 5) % 256, 128])
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) +
            chunk(b"IEND", b""))


def expect_error(fn, label, code=None):
    try:
        fn()
    except RuntimeError as error:
        if code is not None:
            assert ("error %d" % code) in str(error), (label, error)
        return
    raise AssertionError(label + " should fail")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19100)
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--ca", default="")
    parser.add_argument("--proxy", action="store_true")
    parser.add_argument("--files-dir", default="/home/ubuntu/chatbox/files")
    parser.add_argument("--large", action="store_true",
                        help="also run the slow 2GB transfer")
    args = parser.parse_args()

    suffix = str(int(time.time()))
    alice_name = "file_acc_" + suffix
    bob_name = "file_bob_" + suffix
    main = Client(args, args.proxy)
    bob = Client(args, args.proxy)
    register(main, alice_name, "acceptance")
    alice_id, token = login(main, alice_name)
    register(bob, bob_name, "bob")
    bob_id, bob_token = login(bob, bob_name)
    assert alice_id != bob_id

    file_conn = Client(args, args.proxy)
    assert file_auth(file_conn, token) == alice_id
    bob_files = Client(args, args.proxy)
    assert file_auth(bob_files, bob_token) == bob_id

    # 1. Small upload + download round trip.
    small = b"hello acceptance file"
    small_id, small_message, small_upload = upload_file(
        file_conn, 1, 1, "hello.txt", small, "text/plain")
    download_file(file_conn, small_id, hashlib.sha256(small).hexdigest())
    retry_message, retry_file, _ = upload_commit(file_conn, small_upload)
    assert retry_message == small_message and retry_file == small_id

    # 2. Interrupted upload resumes from the server offset.
    large = bytes(range(256)) * 800  # 200 KiB
    digest = hashlib.sha256(large).hexdigest()
    upload_id, offset = upload_init(
        file_conn, 1, 1, "resume.bin", len(large),
        "application/octet-stream", digest)
    offset = upload_chunk(file_conn, upload_id, offset, large[:CHUNK])
    offset = upload_chunk(file_conn, upload_id, offset, large[CHUNK:2 * CHUNK])
    file_conn.close()
    file_conn = Client(args, args.proxy)
    assert file_auth(file_conn, token) == alice_id
    resumed_offset, status = upload_status(file_conn, upload_id)
    assert status == 0 and resumed_offset == 2 * CHUNK, resumed_offset
    while resumed_offset < len(large):
        resumed_offset = upload_chunk(
            file_conn, upload_id, resumed_offset,
            large[resumed_offset:resumed_offset + CHUNK])
    resume_message, resume_file, _ = upload_commit(file_conn, upload_id)
    download_file(file_conn, resume_file, digest)

    # 3. Image thumbnail.
    image = make_png()
    image_id, image_message, _ = upload_file(
        file_conn, 1, 1, "thumb.png", image, "image/png")
    _, _, entries = list_files(file_conn, 1, 1)
    image_entry = [e for e in entries if e["file_id"] == image_id]
    assert image_entry and image_entry[0]["thumb"] == 1, image_entry
    assert image_entry[0]["width"] == 64 and image_entry[0]["height"] == 48
    _, thumb = file_conn.call(CMD_FILE_THUMB, struct.pack(">Q", image_id))
    thumb_reader = Reader(thumb)
    assert thumb_reader.u64() == image_id
    thumb_bytes = thumb_reader.string()
    assert thumb_bytes[:2] == b"\xff\xd8", thumb_bytes[:4]

    # 4. Only the uploader may delete a private/group file (bob is not the uploader).
    expect_error(lambda: bob_files.call(
        CMD_FILE_DELETE, struct.pack(">Q", image_id)), "non-uploader delete", 1003)

    # 5. Delete marks the message as deleted for history readers.
    file_conn.call(CMD_FILE_DELETE, struct.pack(">Q", image_id))
    messages = history(main, 1)
    deleted = [m for m in messages if m["id"] == image_message]
    assert deleted and deleted[0]["content"] == "文件已删除", deleted
    assert all(e["file_id"] != image_id for e in list_files(file_conn, 1, 1)[2])

    # 6. Oversized files are rejected before any bytes are written.
    expect_error(lambda: upload_init(
        file_conn, 1, 1, "too-big.bin", MAX_FILE + 1,
        "application/octet-stream"), "oversized upload", 1001)

    # 7. Group disband removes server files and blocks the old group.
    _, group_body = main.call(CMD_GROUP_CREATE, enc_str("file-accept-" + suffix))
    group = Reader(group_body)
    room_id = group.u32()
    group_file = b"group file payload"
    group_file_id, _, _ = upload_file(
        file_conn, 1, room_id, "group.txt", group_file, "text/plain")
    storage = os.path.join(
        args.files_dir, "%02x" % (group_file_id & 0xFF), str(group_file_id))
    if os.path.isdir(args.files_dir):
        assert os.path.exists(storage), storage
    main.call(CMD_GROUP_DISBAND, struct.pack(">I", room_id))
    if os.path.isdir(args.files_dir):
        assert not os.path.exists(storage), storage
    expect_error(lambda: list_files(file_conn, 1, room_id),
                 "list disbanded group")

    # 8. 0.2.5 compatibility: file messages still carry a text fallback.
    texts = [m["content"] for m in history(main, 1)]
    assert any(t.startswith("[文件] hello.txt") for t in texts), texts[-5:]

    # 9. Optional slow 2GB sparse-style upload. Data is generated, not stored.
    if args.large:
        total = 2 * 1024 * 1024 * 1024
        upload_id, offset = upload_init(
            file_conn, 1, 1, "big.bin", total, "application/octet-stream")
        zero = b"\x00" * CHUNK
        digest = hashlib.sha256()
        while offset < total:
            chunk = zero[:min(CHUNK, total - offset)]
            offset = upload_chunk(file_conn, upload_id, offset, chunk)
            digest.update(chunk)
        _, big_id, _ = upload_commit(file_conn, upload_id)
        info = file_info(file_conn, big_id)
        assert info["size"] == total, info
        assert info["sha256"] == digest.hexdigest(), info["sha256"]
        _, open_body = file_conn.call(CMD_FILE_OPEN, struct.pack(">Q", big_id))
        open_reader = Reader(open_body)
        assert open_reader.u64() == total
        file_conn.call(CMD_FILE_DELETE, struct.pack(">Q", big_id))

    print("FILE_ACCEPTANCE_OK", alice_name, bob_name, small_id, resume_message,
          image_id, "large=" + ("1" if args.large else "0"))
    main.close()
    bob.close()
    file_conn.close()
    bob_files.close()


if __name__ == "__main__":
    main()
