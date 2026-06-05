import QtQuick
import "../../style"

Item {
    id: root

    property bool selected: false
    property bool panelActive: true
    property bool currentItem: false
    property bool hovered: false
    property bool scrolling: false
    property bool resizeOptimized: false
    property real visualOffsetX: 0
    property real leftMargin: 6
    property real rightMargin: 6
    property real topMargin: 2
    property real bottomMargin: 2
    property bool showSelectionBar: true
    property real selectionBarLeftMargin: 4
    property real selectionBarTopMargin: 6
    property real selectionBarBottomMargin: 6
    property real selectionBarWidth: 3
    property real selectionBarRadius: 1.5

    anchors.fill: parent
    transform: Translate { x: root.visualOffsetX }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: root.leftMargin
        anchors.rightMargin: root.rightMargin
        anchors.topMargin: root.topMargin
        anchors.bottomMargin: root.bottomMargin
        radius: Theme.radiusMd

        color: root.selected
               ? (root.panelActive ? Theme.itemSelectedFill : Theme.itemSelectedFillInactive)
               : ((root.hovered && !root.scrolling) ? Theme.itemHoverFill : "transparent")
        border.color: root.selected
                      ? (root.panelActive
                         ? Theme.withAlpha(Theme.itemSelectedBorder, 0.72)
                         : Theme.withAlpha(Theme.itemSelectedBorderInactive, 0.58))
                      : (root.currentItem ? Theme.withAlpha(Theme.focusRing, root.panelActive ? 0.62 : 0.30) : "transparent")
        border.width: root.selected || root.currentItem ? 1 : 0

        Rectangle {
            visible: root.showSelectionBar
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: root.selectionBarTopMargin
            anchors.bottomMargin: root.selectionBarBottomMargin
            anchors.leftMargin: root.selectionBarLeftMargin
            width: root.selected ? root.selectionBarWidth : 0
            radius: root.selectionBarRadius
            color: Theme.accent

            Behavior on width { enabled: !root.resizeOptimized; NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutQuad } }
        }

        Behavior on color { enabled: !root.resizeOptimized; ColorAnimation { duration: Theme.motionFast } }
        Behavior on border.color { enabled: !root.resizeOptimized; ColorAnimation { duration: Theme.motionFast } }
    }
}
