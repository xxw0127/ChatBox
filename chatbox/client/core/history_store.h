#ifndef CHATBOX_CLIENT_CORE_HISTORY_STORE_H
#define CHATBOX_CLIENT_CORE_HISTORY_STORE_H

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "core/chat_message.h"

namespace chatbox {
namespace client {

// 一条会话摘要，用于离线侧栏与最近会话列表。
struct ConversationRow {
    ChatContext context = ChatContext::kRoom;
    quint64 target_id = 0;
    QString title;
    quint64 last_message_id = 0;
    QString last_content;
    QDateTime last_time_utc;
};

// 本地聊天记录库（SQLite / QSQLITE）。按账号隔离：每个账号一个连接名
// history_<user_id>，所有读写 SQL 都必须带 user_id 过滤条件。
// 只在本机保存服务端已确认的消息，不保存未确认的本地待发消息。
class HistoryStore {
public:
    HistoryStore();
    ~HistoryStore();

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    // 打开（或创建）指定目录下的本地库；目录不存在会自动创建。
    // 同一目录 + 同一账号重复调用直接返回 true；换账号或换目录先 close()。
    // 失败时写入非空中文错误文本并返回 false。
    bool open(const QString& directory, quint64 user_id, QString* error);
    void close();
    bool isOpen() const;
    int schemaVersion() const;

    QString directory() const;   // 绝对路径
    quint64 userId() const;

    qint64 sizeBytes() const;    // history.sqlite + -wal + -shm
    qint64 messageCount() const; // 当前 user_id 的消息数
    bool hasMessages() const;

    // 写入一条服务端已确认的消息（message_id > 0）。
    // 按 (user_id, context, target_id, message_id) 去重：同一条消息重复写入
    // 只会更新既有行。message_id == 0（未确认）时返回 false，不写库也不报错。
    // 时间为空时用当前 UTC 落库；recalled 为真时正文写空。
    bool saveMessage(const ChatMessage& message);
    bool markFileDeletedAny(quint64 message_id);

    struct PendingUpload {
        QString upload_id;
        ChatContext context = ChatContext::kRoom;
        quint64 target_id = 0;
        QString source_path;
        QString name;
        quint64 size = 0;
        QString mime;
        QString sha256;
        quint64 offset = 0;
        QString status = QStringLiteral("queued");
    };

    struct FileTask {
        quint64 file_id = 0;
        ChatContext context = ChatContext::kRoom;
        quint64 target_id = 0;
        quint64 message_id = 0;
        QString name;
        QString save_path;
        QString mime;
        QString state;
        quint64 received = 0;
        quint64 total = 0;
    };

    bool savePendingUpload(const PendingUpload& upload);
    bool updatePendingUpload(const QString& upload_id, quint64 offset,
                             const QString& status);
    bool removePendingUpload(const QString& upload_id);
    QVector<PendingUpload> pendingUploads() const;

    bool saveFileTask(const FileTask& task);
    bool updateFileTask(quint64 file_id, ChatContext context,
                        quint64 target_id, const QString& state,
                        quint64 received, quint64 total);
    bool removeFileTask(quint64 file_id, ChatContext context,
                        quint64 target_id);
    QVector<FileTask> pendingFileTasks() const;
    // 标记某条消息已撤回：清空正文并置 recalled；没有匹配行时返回 false。
    // 撤回按会话定位（user_id + context + target_id + message_id）：
    // 房间与私聊的 message_id 各自独立编号，只按 message_id 匹配会误伤
    // 别的会话里同号的消息。
    // 撤回成功后同步重算该会话的摘要（last_content 跟着清空），
    // 离线侧栏不会继续显示已撤回的正文。
    bool markRecalled(ChatContext context, quint64 target_id, quint64 message_id);
    // 写入一条会话摘要：保证会话行存在，title 非空才更新标题；
    // last_* 按本地已存消息重算（本地没有消息时保持 0，不凭空造摘要）。
    bool saveConversation(const ConversationRow& row);
    // 读取当前账号的会话摘要：只返回有消息的会话（last_message_id > 0），
    // 按 last_message_id 倒序（新→旧）；标题为空时回退为「房间 <id>」/「用户 <id>」。
    QVector<ConversationRow> conversations() const;
    // 清理本地副本：只影响当前 user_id，绝不触碰其它账号的数据。
    bool clearAll();               // 清空当前账号的全部消息与会话摘要
    bool clearOlderThan(int days); // 删除当前账号 days 天前的消息（days 夹到 1..365）
    // 切换存储目录：migrate 为真时把现有记录复制到新目录并校验行数，
    // 为假时在新目录建空库（旧目录保留）。目标目录已存在 history.sqlite 时
    // 直接失败并保持当前连接在旧目录上打开；失败时删除本次生成的副本、
    // 重新打开旧目录并返回 false。
    bool moveTo(const QString& new_directory, bool migrate, QString* error);
    // 校验目录可写：不存在则创建，并实际写入临时文件验证；失败时写入中文错误文本。
    static bool ensureWritableDirectory(const QString& directory, QString* error);
    // 读取当前会话最近 limit 条消息，升序（旧→新）返回。
    QVector<ChatMessage> recent(ChatContext context, quint64 target_id,
                                int limit) const;
    // 读取当前会话中 message_id < before_id 的最近 limit 条消息，升序（旧→新）返回。
    QVector<ChatMessage> before(ChatContext context, quint64 target_id,
                                quint64 before_id, int limit) const;
    // 在当前会话内按关键字搜索，按 message_id 倒序（新→旧）返回。
    // 关键字按字面量匹配：% 与 _ 会被转义，不作为通配符；空关键字返回空结果。
    QVector<ChatMessage> search(ChatContext context, quint64 target_id,
                                const QString& keyword, int limit) const;

    static QString defaultDirectory();

private:
    static QString connectionNameFor(quint64 user_id);

    // 保证 (user_id, context, target_id) 的会话行存在。
    bool ensureConversationRow(ChatContext context, quint64 target_id);
    // 按 messages 表里最新一条消息重算某个会话的 last_*；没有消息时置 0。
    bool refreshConversationRow(ChatContext context, quint64 target_id);
    // 写入消息后推进会话摘要：只有新的 message_id 不小于当前 last_message_id
    // 时才更新，更旧的一页历史不得把摘要往回改。
    bool advanceConversationSummary(ChatContext context, quint64 target_id,
                                    quint64 message_id, const QString& content,
                                    const QString& time_text);

    // 读取一页消息（recent / before 共用）：按 message_id 倒序取 limit 条，
    // 再反转为升序返回。has_before 为真时附加 message_id < before_id 条件。
    QVector<ChatMessage> readPage(ChatContext context, quint64 target_id,
                                  bool has_before, quint64 before_id,
                                  int limit) const;

    // 连接名归属登记：同一个 history_<user_id> 同时只能被一个存活实例持有，
    // 否则第二个实例会顺手 removeDatabase 掉第一个实例的连接。
    // 只在 UI 线程使用（与设计一致），因此不需要额外加锁。
    static HistoryStore* connectionOwner(const QString& connection_name);
    static void claimConnection(const QString& connection_name, HistoryStore* owner);
    static void releaseConnection(const QString& connection_name, HistoryStore* owner);

    QString directory_;
    quint64 user_id_ = 0;
    QString connection_name_;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_HISTORY_STORE_H
