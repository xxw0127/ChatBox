#include <string>
#include <vector>

#include "common/file_protocol.h"
#include "tests/test_framework.h"

namespace {

int testUploadInitRoundTrip() {
    chatbox::UploadInitRequest request;
    request.kind = chatbox::FileKind::kRoom;
    request.target_id = 7;
    request.name = "设计稿.psd";
    request.size = chatbox::kMaxFileSize;
    request.mime = "image/vnd.adobe.photoshop";
    request.sha256 = std::string(64, 'a');
    const std::string body = chatbox::encodeUploadInit(request);
    chatbox::UploadInitRequest decoded;
    CHECK_TRUE(chatbox::decodeUploadInit(body, decoded));
    CHECK_EQ(decoded.target_id, 7u);
    CHECK_EQ(decoded.name, std::string("设计稿.psd"));
    CHECK_EQ(decoded.size, chatbox::kMaxFileSize);
    CHECK_TRUE(!chatbox::decodeUploadInit(
        body.substr(0, body.size() - 1), decoded));
    return 0;
}

int testChunksAndOpen() {
    const std::string data(1024, 'x');
    const std::string body = chatbox::encodeUploadChunk("upload-1", 4096, data);
    std::string upload_id;
    uint64_t offset = 0;
    std::string decoded;
    CHECK_TRUE(chatbox::decodeUploadChunk(body, upload_id, offset, decoded));
    CHECK_EQ(upload_id, std::string("upload-1"));
    CHECK_EQ(offset, 4096u);
    CHECK_EQ(decoded.size(), data.size());

    const std::string open = chatbox::encodeFileOpenResponse(2048, "token");
    uint64_t total = 0;
    std::string token;
    CHECK_TRUE(chatbox::decodeFileOpenResponse(open, total, token));
    CHECK_EQ(total, 2048u);
    CHECK_EQ(token, std::string("token"));
    return 0;
}

int testFileListAndMeta() {
    std::vector<chatbox::FileEntry> entries(2);
    entries[0].file_id = 10;
    entries[0].uploader_id = 4;
    entries[0].uploader_name = "阿杰";
    entries[0].message_id = 99;
    entries[0].name = "a.txt";
    entries[0].size = 10;
    entries[0].mime = "text/plain";
    entries[0].sha256 = std::string(64, 'b');
    entries[0].created_at = "2026-10-29 10:00:00";
    entries[0].can_delete = true;
    entries[1].file_id = 11;
    entries[1].name = "b.png";
    const std::string body = chatbox::encodeFileListResponse(entries, 2, 100);
    std::vector<chatbox::FileEntry> decoded;
    uint32_t total = 0;
    uint64_t used = 0;
    CHECK_TRUE(chatbox::decodeFileListResponse(body, decoded, total, used));
    CHECK_EQ(decoded.size(), 2u);
    CHECK_EQ(decoded[0].uploader_name, std::string("阿杰"));
    CHECK_TRUE(decoded[0].can_delete);
    CHECK_EQ(used, 100u);

    std::vector<uint16_t> indices{0, 1};
    std::vector<chatbox::FileMeta> metas(2);
    metas[0].file_id = 10;
    metas[0].name = "a.txt";
    metas[1].file_id = 11;
    metas[1].name = "b.png";
    const std::string table = chatbox::encodeFileMetaTable(indices, metas);
    chatbox::ByteReader reader(table.data(), table.size());
    std::vector<uint16_t> decoded_indices;
    std::vector<chatbox::FileMeta> decoded_metas;
    CHECK_TRUE(chatbox::decodeFileMetaTable(reader, decoded_indices,
                                            decoded_metas));
    CHECK_EQ(decoded_metas.size(), 2u);
    CHECK_EQ(decoded_metas[1].name, std::string("b.png"));
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testUploadInitRoundTrip();
    failures += testChunksAndOpen();
    failures += testFileListAndMeta();
    if (failures == 0) {
        std::cout << "test_file_protocol: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
