import QtQuick
import "../../style"

Rectangle {
    id: root

    property color surfaceColor: Theme.panelSurfaceSoft
    property color strokeColor: Theme.withAlpha(Theme.panelBorder, themeController.isDark ? 0.38 : 0.28)
    property int cornerRadius: Theme.radiusMd
    property int topLeftCornerRadius: cornerRadius
    property int topRightCornerRadius: cornerRadius
    property int bottomLeftCornerRadius: cornerRadius
    property int bottomRightCornerRadius: cornerRadius
    property bool clipped: true

    color: root.surfaceColor
    border.color: root.strokeColor
    border.width: 1
    radius: root.cornerRadius
    topLeftRadius: root.topLeftCornerRadius
    topRightRadius: root.topRightCornerRadius
    bottomLeftRadius: root.bottomLeftCornerRadius
    bottomRightRadius: root.bottomRightCornerRadius
    clip: root.clipped
}
