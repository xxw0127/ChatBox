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
        spacing: 12

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

        Label {
            text: "F0 工具链验证"
            font.pixelSize: 13
            opacity: 0.5
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
