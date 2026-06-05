import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "../style"
import "common"

ToolButton {
    id: btn
    property string iconSource
    property string iconTone: "default"
    property bool isHighlighted: false
    property int iconSize: 18
    property bool svgRecolorEnabled: true
    property color svgRecolorColor: baseTone
    property bool svgRecolorStroke: true
    property bool svgRecolorFill: true

    readonly property color baseTone: Theme.actionIconColor(btn.iconTone)
    readonly property color iconColor: svgRecolorEnabled ? svgRecolorColor : baseTone
    readonly property bool useSvgRecolor: svgRecolorEnabled && iconSource.toLowerCase().endsWith(".svg")
    readonly property bool activeVisual: btn.enabled && btn.isHighlighted
    readonly property color hoverFill: Theme.toolbarButtonFill(iconColor, btn.hovered, btn.pressed, btn.activeVisual)
    readonly property color hoverBorder: Theme.toolbarButtonBorder(iconColor, btn.hovered, btn.activeVisual)
    clip: true
    padding: 0
    
    implicitWidth: 32
    implicitHeight: 32
    
    background: Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Theme.radiusForSide(Math.min(width, height))
        color: btn.hoverFill
        border.color: btn.hoverBorder
        border.width: btn.hovered || btn.activeVisual || btn.pressed ? 1 : 0

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
            width: parent.width - 13
            height: 2
            radius: 1
            visible: btn.activeVisual
            color: Theme.toolbarButtonIndicator(btn.activeVisual)
            opacity: btn.hovered ? 1.0 : 0.88
        }
    }
    
    contentItem: Item {
        implicitWidth: btn.iconSize
        implicitHeight: btn.iconSize
        RecolorSvgIcon {
            id: icon
            anchors.centerIn: parent
            width: btn.iconSize
            height: btn.iconSize
            sourcePath: btn.iconSource
            recolorEnabled: btn.useSvgRecolor
            recolorColor: btn.svgRecolorColor
            recolorStroke: btn.svgRecolorStroke
            recolorFill: btn.svgRecolorFill
            cacheKey: "icon-button"
            sourceSize: Qt.size(36, 36)
            visible: btn.useSvgRecolor
            opacity: btn.useSvgRecolor && !btn.enabled ? 0.45 : 1.0
        }

        MultiEffect {
            anchors.fill: icon
            source: icon
            visible: !btn.useSvgRecolor
            colorization: 1.0
            colorizationColor: btn.iconColor
            opacity: btn.enabled ? 1.0 : 0.45
        }
    }
}
