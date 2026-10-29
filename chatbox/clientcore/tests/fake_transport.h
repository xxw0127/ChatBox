#ifndef CHATBOX_CLIENTCORE_TESTS_FAKE_TRANSPORT_H
#define CHATBOX_CLIENTCORE_TESTS_FAKE_TRANSPORT_H

#include <QByteArray>
#include <QString>
#include <QVector>

#include "common/protocol.h"
#include "transport/transport.h"

class FakeTransport : public chatbox::clientcore::Transport {
    Q_OBJECT

public:
    explicit FakeTransport(QObject* parent = nullptr)
        : chatbox::clientcore::Transport(parent) {}

    bool start(const chatbox::clientcore::TransportConfig&, QString* error) override {
        if (error != nullptr) {
            error->clear();
        }
        connected_ = true;
        emit connected();
        return true;
    }

    void stop() override {
        connected_ = false;
        emit disconnected(QStringLiteral("stopped"));
    }

    bool sendFrame(const QByteArray& frame, QString* error) override {
        if (error != nullptr) {
            error->clear();
        }
        sent.push_back(frame);
        return send_ok_;
    }

    bool isConnected() const override {
        return connected_;
    }

    chatbox::Frame sentFrame(int index) const {
        chatbox::Frame frame;
        const QByteArray& raw = sent.at(index);
        chatbox::FrameHeader header;
        if (!chatbox::decodeHeader(raw.constData(),
                                   static_cast<std::size_t>(raw.size()), header)) {
            return frame;
        }
        frame.header = header;
        frame.body.assign(raw.constData() + chatbox::kFrameHeaderSize,
                          header.body_len);
        return frame;
    }

    void deliver(const chatbox::Frame& frame) {
        const std::string encoded = chatbox::encodeFrame(frame);
        emit frameReceived(
            QByteArray(encoded.data(), static_cast<int>(encoded.size())));
    }

    QVector<QByteArray> sent;
    bool send_ok_ = true;

private:
    bool connected_ = false;
};

#endif  // CHATBOX_CLIENTCORE_TESTS_FAKE_TRANSPORT_H
