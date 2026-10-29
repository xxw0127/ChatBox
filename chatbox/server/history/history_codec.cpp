#include "server/history/history_codec.h"

#include <algorithm>

#include "common/file_protocol.h"

namespace chatbox {

namespace {

std::size_t stringSize(const std::string& value) {
    return 2 + value.size();
}

std::size_t fileMetaSize(const FileMeta& meta) {
    ByteWriter writer;
    appendFileMeta(writer, meta);
    return writer.data().size();
}

}  // namespace

std::size_t historyMessageSize(const HistoryMessage& message) {
    return 8 + 8 + stringSize(message.nickname) + stringSize(message.content) +
           stringSize(message.created_at) + 8 + stringSize(message.reply_sender_name) +
           stringSize(message.reply_content) + 1;
}

bool encodeHistoryMessages(const std::vector<HistoryMessage>& messages,
                           const std::vector<IndexedFileMeta>& file_metas,
                           std::size_t max_bytes, EncodedHistory& encoded,
                           std::string& error) {
    encoded = EncodedHistory{};
    error.clear();
    if (messages.empty()) {
        return true;
    }

    std::size_t sent = 0;
    std::size_t body_size = 0;
    for (const HistoryMessage& message : messages) {
        const std::size_t message_size = historyMessageSize(message);
        if (sent > 0 && body_size + message_size > max_bytes) {
            break;
        }
        body_size += message_size;
        ++sent;
    }
    if (sent == 0) {
        sent = 1;
    }

    std::size_t meta_count = 0;
    for (const IndexedFileMeta& meta : file_metas) {
        if (meta.index >= sent) {
            continue;
        }
        body_size += 2 + fileMetaSize(meta.meta);
        ++meta_count;
    }
    if (body_size > max_bytes && sent > 1) {
        // A large file-meta table can push a full page over budget; drop the
        // last message and its meta until it fits.
        while (sent > 1) {
            --sent;
            body_size = 0;
            for (std::size_t i = 0; i < sent; ++i) {
                body_size += historyMessageSize(messages[i]);
            }
            meta_count = 0;
            for (const IndexedFileMeta& meta : file_metas) {
                if (meta.index >= sent) {
                    continue;
                }
                body_size += 2 + fileMetaSize(meta.meta);
                ++meta_count;
            }
            if (body_size <= max_bytes) {
                break;
            }
        }
    }

    ByteWriter writer;
    for (std::size_t i = 0; i < sent; ++i) {
        const HistoryMessage& message = messages[i];
        writer.putU64(message.id);
        writer.putU64(message.sender_id);
        writer.putString(message.nickname);
        writer.putString(message.content);
        writer.putString(message.created_at);
        writer.putU64(message.reply_to_id);
        writer.putString(message.reply_sender_name);
        writer.putString(message.reply_content);
        writer.putU8(message.recalled ? 1 : 0);
    }
    writer.putU16(static_cast<uint16_t>(meta_count));
    for (const IndexedFileMeta& meta : file_metas) {
        if (meta.index >= sent) {
            continue;
        }
        writer.putU16(meta.index);
        appendFileMeta(writer, meta.meta);
    }

    encoded.body = writer.data();
    encoded.count = static_cast<uint16_t>(sent);
    encoded.has_more = sent < messages.size();
    encoded.next_after = messages[sent - 1].id;
    return true;
}

}  // namespace chatbox
