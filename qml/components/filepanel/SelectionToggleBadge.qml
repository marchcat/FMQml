import QtQuick
import "../../style"

Rectangle {
    id: root

    property var controller: null
    property var panel: null
    property int index: -1
    property bool selected: false
    property bool hovered: false
    property bool currentItem: false
    property bool scrolling: false
    property bool available: true
    property int badgeSize: 18
    property int markSize: 7
    property real markStroke: 1
    readonly property bool panelActive: root.panel ? root.panel.active : true

    width: root.badgeSize
    height: root.badgeSize
    radius: width / 2
    visible: root.available && root.hovered && !root.scrolling && root.panelActive
    opacity: visible ? 1.0 : 0.0
    color: root.selected ? Theme.activeAccent : Theme.withAlpha(Theme.textPrimary, 0.09)
    border.color: root.selected ? Theme.activeAccent : Theme.withAlpha(Theme.textPrimary, 0.4)
    border.width: 1

    Behavior on opacity {
        NumberAnimation { duration: Theme.motionFast }
    }

    Behavior on color {
        ColorAnimation { duration: Theme.motionFast }
    }

    Behavior on border.color {
        ColorAnimation { duration: Theme.motionFast }
    }

    Item {
        id: plusMark
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: root.markSize
        height: root.markSize
        visible: !root.selected

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: root.markStroke
            radius: height / 2
            color: Theme.textPrimary
            opacity: 0.72
        }

        Rectangle {
            anchors.centerIn: parent
            width: root.markStroke
            height: parent.height
            radius: width / 2
            color: Theme.textPrimary
            opacity: 0.72
        }
    }

    Text {
        id: checkText
        anchors.centerIn: parent
        visible: root.selected
        text: "\u2713"
        color: Theme.accentText
        font.pixelSize: Math.max(10, Math.round(root.badgeSize * 0.66))
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

}
