#ifndef CHATBOX_SERVER_AUTH_TOKEN_H
#define CHATBOX_SERVER_AUTH_TOKEN_H

#include <string>

namespace chatbox {

// Returns 64 lowercase hex characters, or an empty string on RNG failure.
std::string generateToken();

// Returns a UUID-shaped session id, or an empty string on RNG failure.
std::string generateSessionId();

// Returns SHA-256(token) as 64 lowercase hex characters.
std::string hashToken(const std::string& token);

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_TOKEN_H
