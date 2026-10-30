#include "core/client_policy.h"

#include <algorithm>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace chatbox {
namespace client {

bool shellNotificationAreaAvailable() {
#ifdef _WIN32
    return ::FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
#else
    return false;
#endif
}

bool shouldStartHidden(bool start_minimized, bool tray_available) {
    return start_minimized && tray_available;
}

bool shouldHideToTray(bool close_to_tray, bool tray_available,
                      bool shutting_down) {
    return close_to_tray && tray_available && !shutting_down;
}

int displayedUnread(bool alerts_enabled, int unread) {
    return alerts_enabled ? std::max(unread, 0) : 0;
}

}  // namespace client
}  // namespace chatbox
