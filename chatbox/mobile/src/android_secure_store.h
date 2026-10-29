#ifndef CHATBOX_MOBILE_ANDROID_SECURE_STORE_H
#define CHATBOX_MOBILE_ANDROID_SECURE_STORE_H

#include "secure/secure_store.h"

namespace chatbox {
namespace mobile {

// Token/device-id storage backed by an AndroidKeyStore AES-GCM key; the
// ciphertext and IV live in SharedPreferences.
class AndroidSecureStore : public chatbox::clientcore::SecureStore {
public:
    QString loadToken() override;
    void saveToken(const QString& token) override;
    void clearToken() override;
    QString loadDeviceId() override;
    void saveDeviceId(const QString& device_id) override;
};

}  // namespace mobile
}  // namespace chatbox

#endif  // CHATBOX_MOBILE_ANDROID_SECURE_STORE_H
