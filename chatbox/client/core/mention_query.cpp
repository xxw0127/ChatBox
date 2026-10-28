#include "core/mention_query.h"

namespace chatbox {
namespace client {

namespace {

bool isMentionBoundary(QChar ch) {
    if (ch.isSpace()) {
        return true;
    }
    // Chinese input rarely inserts a space before '@'; accept the common
    // punctuation marks so "你好，@李硬" still opens the picker.
    static const QString punctuation =
        QString::fromUtf8("\uff0c\u3002\uff01\uff1f\uff1b\uff1a\u3001,.!?;:");
    return punctuation.contains(ch);
}

}  // namespace

MentionToken mentionTokenAt(const QString& text, int cursor) {
    MentionToken token;
    if (cursor <= 0 || cursor > text.size()) {
        return token;
    }

    int at = -1;
    for (int i = cursor - 1; i >= 0; --i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('@')) {
            at = i;
            break;
        }
        if (ch.isSpace()) {
            return token;
        }
    }
    if (at < 0) {
        return token;
    }
    if (at > 0 && !isMentionBoundary(text.at(at - 1))) {
        return token;
    }

    token.active = true;
    token.start = at;
    token.query = text.mid(at + 1, cursor - at - 1);
    return token;
}

}  // namespace client
}  // namespace chatbox
