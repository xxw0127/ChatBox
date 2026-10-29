#include "secure/secure_store.h"

namespace chatbox {
namespace clientcore {

QString MemorySecureStore::loadToken() {
    return token_;
}

void MemorySecureStore::saveToken(const QString& token) {
    token_ = token;
}

void MemorySecureStore::clearToken() {
    token_.clear();
}

QString MemorySecureStore::loadDeviceId() {
    return device_id_;
}

void MemorySecureStore::saveDeviceId(const QString& device_id) {
    device_id_ = device_id;
}

}  // namespace clientcore
}  // namespace chatbox
