import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    background: Rectangle { color: "#f4f5f7" }

    property string connectionState: "disconnected"
    property string errorText: ""
    signal loginRequested(string username, string password)

    readonly property bool busy: connectionState === "connecting"
                                 || connectionState === "authenticating"
                                 || connectionState === "syncing"

    Connections {
        target: ipc
        function onEvent(name, data) {
            if (name === "state" || name === "snapshot") {
                page.connectionState = data.state || page.connectionState
                if (page.connectionState === "online") {
                    page.errorText = ""
                }
            } else if (name === "login_failed" || name === "terminal") {
                page.connectionState = "logged_out"
                page.errorText = data.message || "登录失败"
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
                page.connectionState = data.state
                if (data.state === "online") {
                    page.errorText = ""
                }
            }
        }
    }

    Component.onCompleted: {
        if (ipc.connected) {
            ipc.request("snapshot", {})
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.topMargin: 48
        anchors.bottomMargin: 28
        spacing: 14

        Item { Layout.fillHeight: true }

        Label {
            text: "ChatBox"
            font.pixelSize: 38
            font.bold: true
            color: "#1a1a1a"
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "和电脑上的好友继续聊"
            font.pixelSize: 15
            color: "#8a8f98"
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 18
        }

        TextField {
            id: usernameField
            Layout.fillWidth: true
            placeholderText: "用户名"
            selectByMouse: true
            enabled: !page.busy
        }
        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "密码"
            echoMode: TextInput.Password
            selectByMouse: true
            enabled: !page.busy
            onAccepted: page.tryLogin()
        }

        Label {
            text: page.errorText
            visible: text.length > 0
            color: "#d64545"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Button {
            text: page.busy ? "登录中…" : "登录"
            Layout.fillWidth: true
            Layout.topMargin: 8
            enabled: !page.busy && usernameField.text.length > 0
                     && passwordField.text.length > 0
            onClicked: page.tryLogin()
        }

        Label {
            text: {
                if (!ipc.connected) {
                    return "正在连接本地服务…"
                }
                if (page.connectionState === "reconnecting") {
                    return "连接断开，正在重连…"
                }
                if (page.connectionState === "authenticating") {
                    return "正在验证账号…"
                }
                return ""
            }
            visible: text.length > 0
            color: "#8a8f98"
            font.pixelSize: 13
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        Item { Layout.fillHeight: true }
    }

    function tryLogin() {
        const username = usernameField.text.trim()
        const password = passwordField.text
        if (username.length === 0 || password.length === 0) {
            return
        }
        page.errorText = ""
        page.connectionState = "authenticating"
        page.loginRequested(username, password)
    }
}
