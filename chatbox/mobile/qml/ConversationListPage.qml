import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    background: Rectangle { color: "#f4f5f7" }

    property var conversations: []
    property string connectionState: "disconnected"
    property string nickname: ""

    signal openConversation(int kind, var targetId, string title)
    signal openSettings()

    function formatTime(value) {
        if (!value) {
            return ""
        }
        const date = new Date(value.replace(" ", "T") + "Z")
        if (isNaN(date.getTime())) {
            return value
        }
        return Qt.formatDateTime(date, "MM-dd HH:mm")
    }

    header: ToolBar {
        background: Rectangle { color: "#ffffff" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            Label {
                text: "ChatBox"
                font.pixelSize: 22
                font.bold: true
                color: "#1a1a1a"
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "设置"
                onClicked: page.openSettings()
            }
        }
    }

    Connections {
        target: ipc
        function onEvent(name, data) {
            if (name === "snapshot") {
                page.conversations = data.conversations || []
                page.connectionState = data.state || page.connectionState
                page.nickname = data.nickname || page.nickname
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
                page.conversations = data.conversations || page.conversations
                page.nickname = data.nickname || page.nickname
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
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: visible ? 32 : 0
            visible: !ipc.connected || page.connectionState === "reconnecting"
                     || page.connectionState === "connecting"
                     || page.connectionState === "authenticating"
            color: "#fff3cd"
            Label {
                anchors.centerIn: parent
                text: !ipc.connected ? "本地服务未连接，正在重试…"
                                     : "正在重新连接…"
                font.pixelSize: 12
                color: "#8a6d3b"
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: listView
                anchors.fill: parent
                clip: true
                model: page.conversations
                spacing: 1

            delegate: Rectangle {
                id: row
                required property var modelData
                width: listView.width
                height: 72
                color: rowArea.pressed ? "#eceff2" : "#ffffff"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12

                    Rectangle {
                        width: 44
                        height: 44
                        radius: 22
                        color: modelData.kind === 1 ? "#6c7a89" : "#2aabee"
                        Label {
                            anchors.centerIn: parent
                            text: (modelData.title && modelData.title.length > 0)
                                  ? modelData.title.charAt(0) : "?"
                            color: "#ffffff"
                            font.pixelSize: 18
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.title || (modelData.kind === 1
                                      ? ("群聊 " + modelData.target_id)
                                      : ("用户 " + modelData.target_id))
                                font.pixelSize: 16
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                color: "#1a1a1a"
                            }
                            Label {
                                text: page.formatTime(modelData.last_time)
                                font.pixelSize: 11
                                color: "#9aa0a6"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.last_content || "暂无消息"
                                font.pixelSize: 13
                                color: "#8a8f98"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                width: badgeLabel.implicitWidth + 12
                                height: 20
                                radius: 10
                                color: "#e34b4b"
                                visible: modelData.unread > 0
                                Label {
                                    id: badgeLabel
                                    anchors.centerIn: parent
                                    text: modelData.unread > 99 ? "99+" : modelData.unread
                                    color: "#ffffff"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }
                        }
                    }
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    onClicked: page.openConversation(modelData.kind,
                                                     modelData.target_id,
                                                     modelData.title || "")
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.leftMargin: 72
                    anchors.right: parent.right
                    height: 1
                    color: "#eceff2"
                }
            }
            }

            Label {
                anchors.centerIn: parent
                visible: page.conversations.length === 0
                text: "还没有会话\n在电脑端添加好友后这里会出现"
                color: "#9aa0a6"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
