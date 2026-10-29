#ifndef CHATBOX_SERVER_HISTORY_HISTORY_CODEC_H
#define CHATBOX_SERVER_HISTORY_HISTORY_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/file_types.h"
#include "server/db/mysql_connection.h"

namespace chatbox {

struct IndexedFileMeta {
    uint16_t index = 0;
    FileMeta meta;
};

struct EncodedHistory {
    std::string body;
    uint16_t count = 0;
    bool has_more = false;
    uint64_t next_after = 0;
};

std::size_t historyMessageSize(const HistoryMessage& message);

bool encodeHistoryMessages(const std::vector<HistoryMessage>& messages,
                           const std::vector<IndexedFileMeta>& file_metas,
                           std::size_t max_bytes, EncodedHistory& encoded,
                           std::string& error);

}  // namespace chatbox

#endif  // CHATBOX_SERVER_HISTORY_HISTORY_CODEC_H
