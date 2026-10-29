import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 360
    height: 640
    title: "ChatBox"

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 48
        spacing: 14

        Label {
            text: "ChatBox Android"
            font.pixelSize: 26
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: "Qt " + qtVersion
            font.pixelSize: 15
            opacity: 0.7
            Layout.alignment: Qt.AlignHCenter
        }

        Button {
            text: "连接测试"
            Layout.fillWidth: true
            onClicked: tester.test()
        }

        Label {
            text: tester.status
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        Label {
            text: "F0 工具链验证"
            font.pixelSize: 13
            opacity: 0.5
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
