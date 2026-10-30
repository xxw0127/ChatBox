#ifndef CHATBOX_CLIENT_CORE_CLIENT_POLICY_H
#define CHATBOX_CLIENT_CORE_CLIENT_POLICY_H

namespace chatbox {
namespace client {

// True when Windows' notification area exists (the shell's taskbar window).
// Qt's Windows platform plugin reports QSystemTrayIcon::isSystemTrayAvailable()
// as a constant true, so it cannot detect a missing Explorer shell; that made
// `--minimized` leave an invisible process on machines without a shell.
bool shellNotificationAreaAvailable();

// Start hidden only when the user asked for it and the tray can actually
// present an icon. Extracted so the policy is unit-tested instead of being a
// source guard over main().
bool shouldStartHidden(bool start_minimized, bool tray_available);

// Closing the main window hides it to the tray only when the user enabled
// that behavior, the tray is usable, and this is not an explicit quit.
bool shouldHideToTray(bool close_to_tray, bool tray_available,
                      bool shutting_down);

// Unread counters keep running while alerts are off, but neither the badge nor
// the window-title counter may show them.
int displayedUnread(bool alerts_enabled, int unread);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_CLIENT_POLICY_H
