import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    background: Rectangle { color: "#e9edf2" }

    property int kind: 2
    property var targetId: 0
    property string peerTitle: ""
    property var messages: []
    property string connectionState: "online"

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
                text: page.peerTitle.length > 0 ? page.peerTitle
                                            : ("用户 " + page.targetId)
                font.pixelSize: 18
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: page.connectionState === "online" ? "在线"
                      : (page.connectionState === "reconnecting" ? "重连中" : "")
                font.pixelSize: 12
                color: page.connectionState === "online" ? "#3aa76d" : "#9aa0a6"
            }
        }
    }

    Connections {
        target: ipc
        function onEvent(name, data) {
            if (name === "snapshot") {
                page.connectionState = data.state || page.connectionState
                const active = data.active || {}
                if (active.kind === page.kind
                        && String(active.target_id) === String(page.targetId)) {
                    page.messages = active.messages || []
                }
            } else if (name === "state") {
                page.connectionState = data.state || page.connectionState
            }
        }
        function onConnectedChanged() {
            if (ipc.connected) {
                ipc.request("set_active", {kind: page.kind, target_id: page.targetId})
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
            }
            const active = data.active || {}
            if (active.kind === page.kind
                    && String(active.target_id) === String(page.targetId)) {
                page.messages = active.messages || []
            }
        }
    }

    Component.onCompleted: {
        if (ipc.connected) {
            ipc.request("set_active", {kind: page.kind, target_id: page.targetId})
            ipc.request("snapshot", {})
        }
    }

    onMessagesChanged: {
        listView.positionViewAtEnd()
        markLatestRead()
    }

    function markLatestRead() {
        if (page.messages.length === 0) {
            return
        }
        const last = page.messages[page.messages.length - 1]
        if (last && last.message_id > 0) {
            ipc.request("mark_read", {
                kind: page.kind,
                target_id: page.targetId,
                message_id: last.message_id
            })
        }
    }

    ListView {
        id: listView
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        clip: true
        spacing: 8
        model: page.messages

        delegate: Item {
            id: messageItem
            required property var modelData
            width: listView.width
            height: bubble.height + 6

            Rectangle {
                id: bubble
                anchors.right: modelData.outgoing ? parent.right : undefined
                anchors.left: modelData.outgoing ? undefined : parent.left
                width: Math.min(listView.width * 0.76,
                                bubbleText.implicitWidth + 22)
                height: bubbleText.implicitHeight + 18
                radius: 12
                color: modelData.recalled ? "#d7dbe0"
                     : (modelData.outgoing ? "#2aabee" : "#ffffff")

                Label {
                    id: bubbleText
                    anchors.fill: parent
                    anchors.margins: 9
                    text: {
                        if (modelData.recalled) {
                            return "消息已撤回"
                        }
                        if (modelData.msg_type === 1) {
                            return "[文件]"
                        }
                        return modelData.content || ""
                    }
                    wrapMode: Text.Wrap
                    color: modelData.recalled ? "#7d838a"
                         : (modelData.outgoing ? "#ffffff" : "#1a1a1a")
                    font.pixelSize: 15
                }
            }
        }

        Label {
            anchors.centerIn: parent
            visible: page.messages.length === 0
            text: "还没有消息，打个招呼吧"
            color: "#9aa0a6"
            font.pixelSize: 13
        }
    }

    footer: ToolBar {
        background: Rectangle { color: "#ffffff" }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            TextField {
                id: inputField
                Layout.fillWidth: true
                placeholderText: "输入消息…"
                selectByMouse: true
                enabled: page.connectionState === "online"
                onAccepted: page.send()
            }
            Button {
                text: "发送"
                enabled: inputField.text.trim().length > 0
                         && page.connectionState === "online"
                onClicked: page.send()
            }
        }
    }

    function send() {
        const content = inputField.text.trim()
        if (content.length === 0 || page.connectionState !== "online") {
            return
        }
        ipc.request("send_text", {
            kind: page.kind,
            target_id: page.targetId,
            content: content
        })
        inputField.text = ""
    }
}

