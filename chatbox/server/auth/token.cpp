#include "server/auth/token.h"

#include <cerrno>
#include <cstddef>
#include <openssl/evp.h>
#include <sys/random.h>

namespace chatbox {

namespace {

constexpr std::size_t kTokenBytes = 32;

std::string toHex(const unsigned char* data, std::size_t size) {
    static const char kHex[] = "0123456789abcdef";
    std::string output;
    output.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        output[i * 2] = kHex[(data[i] >> 4) & 0x0F];
        output[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return output;
}

bool fillRandom(unsigned char* buffer, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t received = ::getrandom(buffer + offset, size - offset, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (received == 0) {
            return false;
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

}  // namespace

std::string generateToken() {
    unsigned char random_bytes[kTokenBytes] = {0};
    if (!fillRandom(random_bytes, sizeof(random_bytes))) {
        return {};
    }
    return toHex(random_bytes, sizeof(random_bytes));
}

std::string generateSessionId() {
    const std::string hex = generateToken();
    if (hex.size() != 64) {
        return {};
    }
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) +
           "-" + hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

std::string hashToken(const std::string& token) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned int digest_length = 0;
    if (EVP_Digest(token.data(), token.size(), digest, &digest_length, EVP_sha256(), nullptr) !=
        1) {
        return {};
    }
    return toHex(digest, digest_length);
}

}  // namespace chatbox
