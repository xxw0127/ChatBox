#ifndef CHATBOX_COMMON_FILE_PROTOCOL_H
#define CHATBOX_COMMON_FILE_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

#include "common/file_types.h"
#include "common/protocol.h"

namespace chatbox {

struct UploadInitRequest {
    FileKind kind = FileKind::kRoom;
    uint64_t target_id = 0;
    std::string name;
    uint64_t size = 0;
    std::string mime;
    std::string sha256;
};

std::string encodeUploadInit(const UploadInitRequest& request);
bool decodeUploadInit(const std::string& body, UploadInitRequest& request);
std::string encodeUploadInitResponse(const std::string& upload_id, uint64_t offset);
bool decodeUploadInitResponse(const std::string& body, std::string& upload_id,
                              uint64_t& offset);

std::string encodeUploadChunk(const std::string& upload_id, uint64_t offset,
                              const std::string& data);
bool decodeUploadChunk(const std::string& body, std::string& upload_id,
                       uint64_t& offset, std::string& data);
std::string encodeUploadAck(uint64_t offset);
bool decodeUploadAck(const std::string& body, uint64_t& offset);

std::string encodeUploadStatusResponse(uint64_t offset, uint8_t status);
bool decodeUploadStatusResponse(const std::string& body, uint64_t& offset,
                                uint8_t& status);
std::string encodeUploadCommitResponse(uint64_t message_id, uint64_t file_id,
                                       const std::string& created_at);
bool decodeUploadCommitResponse(const std::string& body, uint64_t& message_id,
                                uint64_t& file_id, std::string& created_at);

std::string encodeFileOpenResponse(uint64_t total_size,
                                   const std::string& range_token);
bool decodeFileOpenResponse(const std::string& body, uint64_t& total_size,
                            std::string& range_token);
std::string encodeFileRead(const std::string& range_token, uint64_t offset,
                           uint32_t length);
bool decodeFileRead(const std::string& body, std::string& range_token,
                    uint64_t& offset, uint32_t& length);
std::string encodeFileChunk(uint64_t file_id, uint64_t offset,
                            const std::string& data);
bool decodeFileChunk(const std::string& body, uint64_t& file_id,
                     uint64_t& offset, std::string& data);
std::string encodeFileReadAck(uint64_t file_id, uint32_t bytes);
bool decodeFileReadAck(const std::string& body, uint64_t& file_id,
                       uint32_t& bytes);

std::string encodeFileListRequest(FileKind kind, uint64_t target_id,
                                  uint64_t before_file_id, uint16_t limit);
bool decodeFileListRequest(const std::string& body, FileKind& kind,
                           uint64_t& target_id, uint64_t& before_file_id,
                           uint16_t& limit);
std::string encodeFileListResponse(const std::vector<FileEntry>& entries,
                                   uint32_t total_count, uint64_t used_bytes);
bool decodeFileListResponse(const std::string& body,
                            std::vector<FileEntry>& entries,
                            uint32_t& total_count, uint64_t& used_bytes);
std::string encodeFileInfo(const FileEntry& entry);
bool decodeFileInfo(const std::string& body, FileEntry& entry);

std::string encodeFileIdResponse(uint64_t file_id);
bool decodeFileIdResponse(const std::string& body, uint64_t& file_id);

void appendFileMeta(ByteWriter& writer, const FileMeta& meta);
bool readFileMeta(ByteReader& reader, FileMeta& meta);
std::string encodeFileMetaTable(const std::vector<uint16_t>& indices,
                                const std::vector<FileMeta>& metas);
bool decodeFileMetaTable(ByteReader& reader, std::vector<uint16_t>& indices,
                         std::vector<FileMeta>& metas);

}  // namespace chatbox

#endif  // CHATBOX_COMMON_FILE_PROTOCOL_H
