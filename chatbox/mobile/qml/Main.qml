import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 400
    height: 800
    minimumWidth: 320
    minimumHeight: 480
    title: "ChatBox"

    property string pageName: ""
    property string chatState: "disconnected"
    property var conversations: []

    function loadPage(name, properties) {
        window.pageName = name
        pageLoader.setSource(name + ".qml", properties || {})
    }

    function conversationTitle(kind, targetId) {
        for (let i = 0; i < window.conversations.length; ++i) {
            const item = window.conversations[i]
            if (item.kind === kind && String(item.target_id) === String(targetId)) {
                return item.title || ""
            }
        }
        return ""
    }

    function checkPendingConversation() {
        if (window.chatState !== "online") {
            return
        }
        if (window.pageName === "" || window.pageName === "ChatPage"
                || window.pageName === "SettingsPage") {
            return
        }
        const pending = ipc.takePendingConversation()
        if (!pending || !pending.kind || !pending.target_id) {
            return
        }
        window.loadPage("ChatPage", {
            kind: pending.kind,
            targetId: pending.target_id,
            peerTitle: window.conversationTitle(pending.kind, pending.target_id)
        })
        ipc.request("set_active", {
            kind: pending.kind,
            target_id: pending.target_id
        })
    }

    function applyState(state) {
        if (!state) {
            return
        }
        window.chatState = state
        if (state === "online" || state === "syncing") {
            if (window.pageName === "" || window.pageName === "LoginPage") {
                window.loadPage("ConversationListPage")
                ipc.request("snapshot", {})
            }
            if (state === "online") {
                window.checkPendingConversation()
            }
        } else if (state === "logged_out" || state === "disconnected") {
            if (window.pageName !== "LoginPage") {
                window.loadPage("LoginPage")
            }
        }
    }

    Connections {
        target: ipc
        function onEvent(name, data) {
            if (name === "state") {
                window.applyState(data.state)
            } else if (name === "snapshot") {
                window.conversations = data.conversations || window.conversations
                window.applyState(data.state)
            } else if (name === "terminal") {
                window.applyState("logged_out")
            }
        }
        function onConnectedChanged() {
            if (ipc.connected) {
                ipc.request("snapshot", {})
            }
        }
        function onResponse(id, response) {
            if (!response.ok) {
                return
            }
            const data = response.data || {}
            if (data.state) {
                window.conversations = data.conversations || window.conversations
                window.applyState(data.state)
            }
        }
    }

    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) {
                if (window.pageName === "ChatPage" && pageLoader.item) {
                    ipc.request("set_active", {
                        kind: pageLoader.item.kind,
                        target_id: pageLoader.item.targetId
                    })
                }
                window.checkPendingConversation()
            } else {
                // Backgrounded or locked: stop suppressing notifications for the
                // conversation that happened to be open.
                ipc.request("set_active", {kind: 0, target_id: 0})
            }
        }
    }

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: window.checkPendingConversation()
    }

    Component.onCompleted: {
        window.loadPage("LoginPage")
        ipc.requestNotifications()
        if (ipc.connected) {
            ipc.request("snapshot", {})
        }
    }

    Loader {
        id: pageLoader
        anchors.fill: parent
        // Qt 6.8 has no SafeArea API; keep the header below the Android status
        // bar (about 28 dp on the tested device).
        anchors.topMargin: 28

        onLoaded: {
            if (window.pageName === "LoginPage") {
                item.loginRequested.connect(function(username, password) {
                    ipc.request("login", {username: username, password: password})
                })
            } else if (window.pageName === "ConversationListPage") {
                item.openConversation.connect(function(kind, targetId, title) {
                    window.loadPage("ChatPage", {
                        kind: kind,
                        targetId: targetId,
                        peerTitle: title
                    })
                    ipc.request("set_active", {kind: kind, target_id: targetId})
                    ipc.request("snapshot", {})
                })
                item.openSettings.connect(function() {
                    window.loadPage("SettingsPage")
                })
            } else if (window.pageName === "ChatPage") {
                item.back.connect(function() {
                    ipc.request("set_active", {kind: 0, target_id: 0})
                    window.loadPage("ConversationListPage")
                    ipc.request("snapshot", {})
                })
            } else if (window.pageName === "SettingsPage") {
                item.back.connect(function() {
                    window.loadPage("ConversationListPage")
                    ipc.request("snapshot", {})
                })
            }
        }
    }
}

