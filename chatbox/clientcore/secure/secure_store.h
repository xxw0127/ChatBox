#ifndef CHATBOX_CLIENTCORE_SECURE_SECURE_STORE_H
#define CHATBOX_CLIENTCORE_SECURE_SECURE_STORE_H

#include <QString>

namespace chatbox {
namespace clientcore {

// Token and per-install device id storage. The Android implementation wraps
// the platform Keystore; tests use MemorySecureStore.
class SecureStore {
public:
    virtual ~SecureStore() = default;

    virtual QString loadToken() = 0;
    virtual void saveToken(const QString& token) = 0;
    virtual void clearToken() = 0;

    virtual QString loadDeviceId() = 0;
    virtual void saveDeviceId(const QString& device_id) = 0;
};

class MemorySecureStore : public SecureStore {
public:
    QString loadToken() override;
    void saveToken(const QString& token) override;
    void clearToken() override;
    QString loadDeviceId() override;
    void saveDeviceId(const QString& device_id) override;

private:
    QString token_;
    QString device_id_;
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_SECURE_SECURE_STORE_H
