import QtQuick
import "../../style"

Item {
    id: root

    property bool active: false
    property bool allowed: false
    property string deniedReason: ""

    visible: active
    opacity: active ? 1 : 0

    Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Theme.innerRadius(Theme.panelRadius, 1)
        color: root.allowed
               ? Theme.withAlpha(Theme.accent, themeController.isDark ? 0.10 : 0.075)
               : Theme.withAlpha(Theme.danger, themeController.isDark ? 0.10 : 0.075)
        border.color: root.allowed
                      ? Theme.withAlpha(Theme.accent, themeController.isDark ? 0.58 : 0.44)
                      : Theme.withAlpha(Theme.danger, themeController.isDark ? 0.58 : 0.44)
        border.width: 1
        antialiasing: true
    }
}
