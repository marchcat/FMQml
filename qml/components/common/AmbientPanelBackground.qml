import QtQuick
import "../../style"

Rectangle {
    id: root

    property color baseColor: Theme.panelSurface
    property color startColor: Theme.chromeGradientStart
    property color midColor: Theme.chromeGradientMid
    property color endColor: Theme.withAlpha(Theme.panelSurface, themeController.isDark ? 0.88 : 0.82)
    property real strength: 1.0
    property int cornerRadius: 0
    property int topLeftCornerRadius: cornerRadius
    property int topRightCornerRadius: cornerRadius
    property int bottomLeftCornerRadius: cornerRadius
    property int bottomRightCornerRadius: cornerRadius

    color: root.baseColor
    radius: root.cornerRadius
    topLeftRadius: root.topLeftCornerRadius
    topRightRadius: root.topRightCornerRadius
    bottomLeftRadius: root.bottomLeftCornerRadius
    bottomRightRadius: root.bottomRightCornerRadius
    antialiasing: true

    Rectangle {
        anchors.fill: parent
        anchors.margins: root.border.width
        radius: Theme.innerRadius(root.cornerRadius, root.border.width)
        topLeftRadius: Theme.innerRadius(root.topLeftCornerRadius, root.border.width)
        topRightRadius: Theme.innerRadius(root.topRightCornerRadius, root.border.width)
        bottomLeftRadius: Theme.innerRadius(root.bottomLeftCornerRadius, root.border.width)
        bottomRightRadius: Theme.innerRadius(root.bottomRightCornerRadius, root.border.width)
        visible: Theme.useGradientColors
        opacity: root.strength
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: root.startColor }
            GradientStop { position: 0.42; color: root.midColor }
            GradientStop { position: 1.0; color: root.endColor }
        }
    }
}
