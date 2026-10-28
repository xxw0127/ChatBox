#include "common/file_protocol.h"

#include <algorithm>

namespace chatbox {

namespace {

void putEntry(ByteWriter& writer, const FileEntry& entry) {
    writer.putU64(entry.file_id);
    writer.putU64(entry.uploader_id);
    writer.putString(entry.uploader_name);
    writer.putU64(entry.message_id);
    writer.putString(entry.name);
    writer.putU64(entry.size);
    writer.putString(entry.mime);
    writer.putString(entry.sha256);
    writer.putU8(entry.thumb_available ? 1 : 0);
    writer.putU32(entry.width);
    writer.putU32(entry.height);
    writer.putU32(entry.duration_ms);
    writer.putString(entry.created_at);
    writer.putU8(entry.can_delete ? 1 : 0);
}

bool getEntry(ByteReader& reader, FileEntry& entry) {
    uint8_t thumb = 0;
    uint8_t can_delete = 0;
    if (!reader.getU64(entry.file_id) || !reader.getU64(entry.uploader_id) ||
        !reader.getString(entry.uploader_name) ||
        !reader.getU64(entry.message_id) || !reader.getString(entry.name) ||
        !reader.getU64(entry.size) || !reader.getString(entry.mime) ||
        !reader.getString(entry.sha256) || !reader.getU8(thumb) ||
        !reader.getU32(entry.width) || !reader.getU32(entry.height) ||
        !reader.getU32(entry.duration_ms) || !reader.getString(entry.created_at) ||
        !reader.getU8(can_delete)) {
        return false;
    }
    entry.thumb_available = thumb != 0;
    entry.can_delete = can_delete != 0;
    return true;
}

}  // namespace

std::string encodeUploadInit(const UploadInitRequest& request) {
    ByteWriter writer;
    writer.putU8(static_cast<uint8_t>(request.kind));
    writer.putU64(request.target_id);
    writer.putString(request.name);
    writer.putU64(request.size);
    writer.putString(request.mime);
    writer.putString(request.sha256);
    return writer.data();
}

bool decodeUploadInit(const std::string& body, UploadInitRequest& request) {
    ByteReader reader(body.data(), body.size());
    uint8_t kind = 0;
    if (!reader.getU8(kind) || !reader.getU64(request.target_id) ||
        !reader.getString(request.name) || !reader.getU64(request.size) ||
        !reader.getString(request.mime) || !reader.getString(request.sha256)) {
        return false;
    }
    request.kind = static_cast<FileKind>(kind);
    return true;
}

std::string encodeUploadInitResponse(const std::string& upload_id,
                                     uint64_t offset) {
    ByteWriter writer;
    writer.putString(upload_id);
    writer.putU64(offset);
    return writer.data();
}

bool decodeUploadInitResponse(const std::string& body, std::string& upload_id,
                              uint64_t& offset) {
    ByteReader reader(body.data(), body.size());
    return reader.getString(upload_id) && reader.getU64(offset);
}

std::string encodeUploadChunk(const std::string& upload_id, uint64_t offset,
                              const std::string& data) {
    ByteWriter writer;
    writer.putString(upload_id);
    writer.putU64(offset);
    writer.putString(data);
    return writer.data();
}

bool decodeUploadChunk(const std::string& body, std::string& upload_id,
                       uint64_t& offset, std::string& data) {
    ByteReader reader(body.data(), body.size());
    return reader.getString(upload_id) && reader.getU64(offset) &&
           reader.getString(data);
}

std::string encodeUploadAck(uint64_t offset) {
    ByteWriter writer;
    writer.putU64(offset);
    return writer.data();
}

bool decodeUploadAck(const std::string& body, uint64_t& offset) {
    ByteReader reader(body.data(), body.size());
    return reader.getU64(offset);
}

std::string encodeUploadStatusResponse(uint64_t offset, uint8_t status) {
    ByteWriter writer;
    writer.putU64(offset);
    writer.putU8(status);
    return writer.data();
}

bool decodeUploadStatusResponse(const std::string& body, uint64_t& offset,
                                uint8_t& status) {
    ByteReader reader(body.data(), body.size());
    return reader.getU64(offset) && reader.getU8(status);
}

std::string encodeUploadCommitResponse(uint64_t message_id, uint64_t file_id,
                                       const std::string& created_at) {
    ByteWriter writer;
    writer.putU64(message_id);
    writer.putU64(file_id);
    writer.putString(created_at);
    return writer.data();
}

bool decodeUploadCommitResponse(const std::string& body, uint64_t& message_id,
                                uint64_t& file_id, std::string& created_at) {
    ByteReader reader(body.data(), body.size());
    return reader.getU64(message_id) && reader.getU64(file_id) &&
           reader.getString(created_at);
}

std::string encodeFileOpenResponse(uint64_t total_size,
                                   const std::string& range_token) {
    ByteWriter writer;
    writer.putU64(total_size);
    writer.putString(range_token);
    return writer.data();
}

bool decodeFileOpenResponse(const std::string& body, uint64_t& total_size,
                            std::string& range_token) {
    ByteReader reader(body.data(), body.size());
    return reader.getU64(total_size) && reader.getString(range_token);
}

std::string encodeFileRead(const std::string& range_token, uint64_t offset,
                           uint32_t length) {
    ByteWriter writer;
    writer.putString(range_token);
    writer.putU64(offset);
    writer.putU32(length);
    return writer.data();
}

bool decodeFileRead(const std::string& body, std::string& range_token,
                    uint64_t& offset, uint32_t& length) {
    ByteReader reader(body.data(), body.size());
    return reader.getString(range_token) && reader.getU64(offset) &&
           reader.getU32(length);
}

std::string encodeFileChunk(uint64_t file_id, uint64_t offset,
                            const std::string& data) {
    ByteWriter writer;
    writer.putU64(file_id);
    writer.putU64(offset);
    writer.putString(data);
    return writer.data();
}

bool decodeFileChunk(const std::string& body, uint64_t& file_id,
                     uint64_t& offset, std::string& data) {
    ByteReader reader(body.data(), body.size());
    return reader.getU64(file_id) && reader.getU64(offset) &&
           reader.getString(data);
}

std::string encodeFileReadAck(uint64_t file_id, uint32_t bytes) {
    ByteWriter writer;
    writer.putU64(file_id);
    writer.putU32(bytes);
    return writer.data();
}

bool decodeFileReadAck(const std::string& body, uint64_t& file_id,
                       uint32_t& bytes) {
    ByteReader reader(body.data(), body.size());
    return reader.getU64(file_id) && reader.getU32(bytes);
}

std::string encodeFileListRequest(FileKind kind, uint64_t target_id,
                                  uint64_t before_file_id, uint16_t limit) {
    ByteWriter writer;
    writer.putU8(static_cast<uint8_t>(kind));
    writer.putU64(target_id);
    writer.putU64(before_file_id);
    writer.putU16(limit);
    return writer.data();
}

bool decodeFileListRequest(const std::string& body, FileKind& kind,
                           uint64_t& target_id, uint64_t& before_file_id,
                           uint16_t& limit) {
    ByteReader reader(body.data(), body.size());
    uint8_t value = 0;
    if (!reader.getU8(value) || !reader.getU64(target_id) ||
        !reader.getU64(before_file_id) || !reader.getU16(limit)) {
        return false;
    }
    kind = static_cast<FileKind>(value);
    return true;
}

std::string encodeFileListResponse(const std::vector<FileEntry>& entries,
                                   uint32_t total_count, uint64_t used_bytes) {
    ByteWriter writer;
    writer.putU32(total_count);
    writer.putU64(used_bytes);
    const std::size_t count = std::min<std::size_t>(entries.size(), 0xFFFF);
    writer.putU16(static_cast<uint16_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        putEntry(writer, entries[i]);
    }
    return writer.data();
}

bool decodeFileListResponse(const std::string& body,
                            std::vector<FileEntry>& entries,
                            uint32_t& total_count, uint64_t& used_bytes) {
    ByteReader reader(body.data(), body.size());
    uint16_t count = 0;
    if (!reader.getU32(total_count) || !reader.getU64(used_bytes) ||
        !reader.getU16(count)) {
        return false;
    }
    entries.clear();
    entries.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        FileEntry entry;
        if (!getEntry(reader, entry)) {
            return false;
        }
        entries.push_back(std::move(entry));
    }
    return true;
}

std::string encodeFileInfo(const FileEntry& entry) {
    ByteWriter writer;
    putEntry(writer, entry);
    return writer.data();
}

bool decodeFileInfo(const std::string& body, FileEntry& entry) {
    ByteReader reader(body.data(), body.size());
    return getEntry(reader, entry);
}

std::string encodeFileIdResponse(uint64_t file_id) {
    ByteWriter writer;
    writer.putU64(file_id);
    return writer.data();
}

bool decodeFileIdResponse(const std::string& body, uint64_t& file_id) {
    ByteReader reader(body.data(), body.size());
    return reader.getU64(file_id);
}

void appendFileMeta(ByteWriter& writer, const FileMeta& meta) {
    writer.putU8(1);
    writer.putU64(meta.file_id);
    writer.putString(meta.name);
    writer.putU64(meta.size);
    writer.putString(meta.mime);
    writer.putU8(meta.thumb_available ? 1 : 0);
    writer.putU32(meta.width);
    writer.putU32(meta.height);
    writer.putU32(meta.duration_ms);
}

bool readFileMeta(ByteReader& reader, FileMeta& meta) {
    uint8_t msg_type = 0;
    uint8_t thumb = 0;
    if (!reader.getU8(msg_type)) {
        return false;
    }
    if (msg_type == 0) {
        meta = FileMeta{};
        return true;
    }
    if (!reader.getU64(meta.file_id) || !reader.getString(meta.name) ||
        !reader.getU64(meta.size) || !reader.getString(meta.mime) ||
        !reader.getU8(thumb) || !reader.getU32(meta.width) ||
        !reader.getU32(meta.height) || !reader.getU32(meta.duration_ms)) {
        return false;
    }
    meta.thumb_available = thumb != 0;
    return true;
}

std::string encodeFileMetaTable(const std::vector<uint16_t>& indices,
                                const std::vector<FileMeta>& metas) {
    ByteWriter writer;
    const std::size_t count = std::min(indices.size(), metas.size());
    writer.putU16(static_cast<uint16_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        writer.putU16(indices[i]);
        appendFileMeta(writer, metas[i]);
    }
    return writer.data();
}

bool decodeFileMetaTable(ByteReader& reader, std::vector<uint16_t>& indices,
                         std::vector<FileMeta>& metas) {
    uint16_t count = 0;
    if (!reader.getU16(count)) {
        return false;
    }
    indices.clear();
    metas.clear();
    indices.reserve(count);
    metas.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t index = 0;
        FileMeta meta;
        if (!reader.getU16(index) || !readFileMeta(reader, meta)) {
            return false;
        }
        indices.push_back(index);
        metas.push_back(std::move(meta));
    }
    return true;
}

}  // namespace chatbox
