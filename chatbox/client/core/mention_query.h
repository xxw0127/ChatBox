#ifndef CHATBOX_CLIENT_CORE_MENTION_QUERY_H
#define CHATBOX_CLIENT_CORE_MENTION_QUERY_H

#include <QString>

namespace chatbox {
namespace client {

// The "@query" fragment immediately before the caret, if the caret is inside
// one. `start` is the index of '@'; `query` is the text between '@' and the
// caret (may be empty).
struct MentionToken {
    bool active = false;
    int start = -1;
    QString query;
};

// Recognises a mention token at `cursor`. '@' must start the text or follow
// whitespace / common CJK or ASCII punctuation, so e-mail addresses and
// mid-word '@' do not open the picker.
MentionToken mentionTokenAt(const QString& text, int cursor);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_MENTION_QUERY_H
