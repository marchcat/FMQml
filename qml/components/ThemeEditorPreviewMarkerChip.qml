import "../style"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "common"

Rectangle {
    property string title: ""
    property string detail: ""
    property color accent: Theme.accent
    property bool emphasized: false
    property bool compact: false

    visible: title.length > 0 || detail.length > 0
    implicitHeight: markerColumn.implicitHeight + 10
    implicitWidth: compact ? Math.max(72, markerTitle.implicitWidth + 14) : Math.max(96, Math.min(220, markerColumn.implicitWidth + 14))
    radius: 10
    color: Theme.withAlpha(accent, emphasized ? 0.2 : 0.12)
    border.color: Theme.withAlpha(accent, emphasized ? 0.74 : 0.4)
    border.width: 1

    Column {
        id: markerColumn

        anchors.fill: parent
        anchors.margins: 5
        spacing: 1

        Label {
            id: markerTitle

            visible: parent.parent.title.length > 0
            text: parent.parent.title
            color: parent.parent.accent
            font.pixelSize: Theme.scaledSize(9)
            font.weight: Font.Bold
        }

        Label {
            visible: !parent.parent.compact && parent.parent.detail.length > 0
            text: parent.parent.detail
            color: Theme.textPrimary
            font.pixelSize: Theme.scaledSize(8)
            elide: Text.ElideRight
            maximumLineCount: 1
        }

    }

}
