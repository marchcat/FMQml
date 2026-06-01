import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../style"

Item {
    id: root

    property string iconSource: "qrc:/qt/qml/FM/qml/assets/icons/archive.svg"
    property bool compact: true

    clip: true

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, root.compact ? 280 : 360)
        spacing: root.compact ? 10 : 14

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: root.compact ? 70 : 88
            height: width
            radius: 16
            color: Theme.withAlpha(Theme.secondaryAccent, themeController.isDark ? 0.18 : 0.12)
            border.color: Theme.withAlpha(Theme.secondaryAccent, 0.35)
            border.width: 1

            Image {
                anchors.centerIn: parent
                source: root.iconSource
                sourceSize: Qt.size(root.compact ? 36 : 46, root.compact ? 36 : 46)
                smooth: true
                opacity: 0.9
            }
        }

        Label {
            Layout.fillWidth: true
            text: "Archive Preview Limited"
            font.bold: true
            font.pixelSize: root.compact ? 14 : 17
            color: Theme.textPrimary
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            text: "Inside archives, QuickLook and preview only handle small lightweight files."
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            font.pixelSize: root.compact ? 11 : 12
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
