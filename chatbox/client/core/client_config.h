#ifndef CHATBOX_CLIENT_CORE_CLIENT_CONFIG_H
#define CHATBOX_CLIENT_CORE_CLIENT_CONFIG_H

#include <QtGlobal>

// Production endpoint. Builds used by QA may override these with qmake
// DEFINES, but the shipped client never exposes an address editor.
#ifndef CHATBOX_SERVER_HOST
#define CHATBOX_SERVER_HOST "121.5.168.189"
#endif
#ifndef CHATBOX_SERVER_PORT
#define CHATBOX_SERVER_PORT 9443
#endif
#ifndef CHATBOX_SERVER_TLS
#define CHATBOX_SERVER_TLS 1
#endif
#ifndef CHATBOX_UPDATE_MANIFEST
#define CHATBOX_UPDATE_MANIFEST "https://121.5.168.189/updates/latest.json"
#endif

namespace chatbox {
namespace client {

inline constexpr const char* kServerHost = CHATBOX_SERVER_HOST;
inline constexpr quint16 kServerPort = CHATBOX_SERVER_PORT;
inline constexpr bool kServerTls = CHATBOX_SERVER_TLS != 0;
inline constexpr const char* kUpdateManifestUrl = CHATBOX_UPDATE_MANIFEST;

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_CLIENT_CONFIG_H
