import "../style"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "common"

Button {
    id: modeButton

    property string title: ""
    property bool selected: false
    property color accentColor: Theme.accent

    text: title
    implicitHeight: 34
    implicitWidth: 88

    contentItem: Label {
        text: modeButton.text
        opacity: modeButton.enabled ? 1 : 0.55
        color: modeButton.selected ? Theme.accentText : Theme.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Theme.fontSizeLabel
        font.weight: modeButton.selected ? Font.DemiBold : Font.Medium
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: modeButton.selected ? modeButton.accentColor : (modeButton.hovered ? Theme.controlSurfaceActive : Theme.controlSurface)
        border.color: modeButton.selected ? modeButton.accentColor : Theme.controlBorder
        opacity: modeButton.enabled ? 1 : 0.62
        border.width: 1
    }

}
