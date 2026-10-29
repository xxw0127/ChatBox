import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    background: Rectangle { color: "#f4f5f7" }

    property string connectionState: "disconnected"
    property string nickname: ""
    property var userId: 0

    signal back()

    header: ToolBar {
        background: Rectangle { color: "#ffffff" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 12
            ToolButton {
                text: "‹"
                font.pixelSize: 26
                onClicked: page.back()
            }
            Label {
                text: "设置"
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
            }
        }
    }

    Connections {
        target: ipc
        function onEvent(name, data) {
            if (name === "snapshot") {
                page.connectionState = data.state || page.connectionState
                page.nickname = data.nickname || page.nickname
                page.userId = data.user_id || page.userId
            } else if (name === "state") {
                page.connectionState = data.state || page.connectionState
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
                page.nickname = data.nickname || page.nickname
                page.userId = data.user_id || page.userId
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
        anchors.margins: 16
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            height: 84
            radius: 12
            color: "#ffffff"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                Rectangle {
                    width: 48
                    height: 48
                    radius: 24
                    color: "#2aabee"
                    Label {
                        anchors.centerIn: parent
                        text: page.nickname.length > 0
                              ? page.nickname.charAt(0) : "?"
                        color: "#ffffff"
                        font.pixelSize: 20
                        font.bold: true
                    }
                }
                ColumnLayout {
                    spacing: 2
                    Label {
                        text: page.nickname.length > 0 ? page.nickname
                                                       : "未登录"
                        font.pixelSize: 17
                        font.bold: true
                    }
                    Label {
                        text: page.userId > 0 ? ("ID " + page.userId)
                                              : "本地账号"
                        font.pixelSize: 12
                        color: "#8a8f98"
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        Label {
            text: "连接"
            font.pixelSize: 13
            color: "#8a8f98"
            Layout.topMargin: 8
        }
        Rectangle {
            Layout.fillWidth: true
            height: 48
            radius: 10
            color: "#ffffff"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                Label { text: "状态"; Layout.fillWidth: true }
                Label {
                    text: {
                        switch (page.connectionState) {
                        case "online": return "已连接"
                        case "reconnecting": return "重连中"
                        case "connecting":
                        case "authenticating":
                        case "syncing": return "连接中"
                        default: return "未连接"
                        }
                    }
                    color: page.connectionState === "online" ? "#3aa76d" : "#8a8f98"
                }
            }
        }

        Label {
            text: "通知与后台"
            font.pixelSize: 13
            color: "#8a8f98"
            Layout.topMargin: 8
        }
        Button {
            text: "允许通知"
            Layout.fillWidth: true
            onClicked: ipc.requestNotifications()
        }
        Button {
            text: "打开系统通知设置"
            Layout.fillWidth: true
            onClicked: ipc.openNotificationSettings()
        }
        Button {
            text: "电池优化设置"
            Layout.fillWidth: true
            onClicked: ipc.openBatterySettings()
        }
        Label {
            text: "小米/红米需在系统设置里允许「自启动」，否则重启后服务不会自动上线。"
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: "#9aa0a6"
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        Button {
            text: "退出登录"
            Layout.fillWidth: true
            onClicked: ipc.request("logout", {})
        }
    }
}
