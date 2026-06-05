import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../style"

Item {
    id: root

    property string sourcePath: ""
    property string name: ""
    property string sizeText: ""
    property string modifiedText: ""
    property string mimeName: ""
    property string extension: ""
    property int sourceSizeWidth: 2048
    property int sourceSizeHeight: 2048
    property string loadingText: "Loading video preview..."
    property bool showBusyIndicator: true
    property bool compact: false

    readonly property int imageStatus: previewImage.imageStatus
    readonly property bool thumbnailReady: previewImage.imageStatus === Image.Ready
    readonly property bool thumbnailLoading: previewImage.imageStatus === Image.Loading
    readonly property string formatText: extension.length > 0 ? extension.toUpperCase() : "VIDEO"
    readonly property string titleText: name.length > 0 ? name : "Video File"
    readonly property string subtitleText: mimeName.length > 0 ? mimeName : "Video preview"
    readonly property string metaText: {
        if (sizeText.length > 0 && modifiedText.length > 0) return sizeText + "  |  " + modifiedText
        if (sizeText.length > 0) return sizeText
        if (modifiedText.length > 0) return modifiedText
        return formatText
    }

    clip: true

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: !root.thumbnailReady

        Rectangle {
            anchors.fill: parent
            anchors.margins: 0
            radius: Theme.panelRadius
            color: Theme.withAlpha(Theme.secondaryAccent, themeController.isDark ? 0.12 : 0.09)
            border.color: Theme.withAlpha(Theme.secondaryAccent, themeController.isDark ? 0.34 : 0.26)
            border.width: 1
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: root.compact ? 10 : 18
                spacing: root.compact ? 10 : 18

                Rectangle {
                    Layout.preferredWidth: root.compact ? 62 : 116
                    Layout.preferredHeight: width
                    Layout.maximumWidth: Math.min(parent.width * 0.34, root.compact ? 62 : 116)
                    Layout.maximumHeight: Layout.maximumWidth
                    radius: Theme.radiusLg
                    color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.16 : 0.12)
                    border.color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.42 : 0.30)
                    border.width: 1

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/qt/qml/FM/qml/assets/icons/video.svg"
                        sourceSize: Qt.size(root.compact ? 34 : 58, root.compact ? 34 : 58)
                        opacity: 0.92
                        smooth: true
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: root.compact ? 5 : 7
                        width: formatLabel.implicitWidth + 12
                        height: root.compact ? 18 : 20
                        radius: Theme.radiusSm
                        color: Theme.withAlpha(Theme.bg, themeController.isDark ? 0.72 : 0.82)

                        Label {
                            id: formatLabel
                            anchors.centerIn: parent
                            text: root.formatText
                            font.pixelSize: root.compact ? 8 : 9
                            font.bold: true
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: root.compact ? 3 : 7

                    Label {
                        Layout.fillWidth: true
                        text: root.titleText
                        font.pixelSize: root.compact ? 14 : 22
                        font.bold: true
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.subtitleText
                        font.pixelSize: root.compact ? 11 : 13
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.metaText
                        font.pixelSize: root.compact ? 10 : 11
                        color: Theme.textSecondary
                        opacity: 0.84
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        Layout.preferredWidth: statusText.implicitWidth + 18
                        Layout.preferredHeight: root.compact ? 22 : 24
                        radius: Theme.radiusSm
                        color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.13 : 0.10)
                        border.color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.32 : 0.24)
                        border.width: 1

                        Label {
                            id: statusText
                            anchors.centerIn: parent
                            text: root.thumbnailLoading ? "Loading thumbnail" : "Preview unavailable"
                            font.pixelSize: root.compact ? 9 : 10
                            font.bold: true
                            color: Theme.accent
                        }
                    }
                }
            }
        }
    }

    ImagePreview {
        id: previewImage
        anchors.fill: parent
        sourcePath: root.sourcePath
        fillMode: Image.PreserveAspectFit
        sourceSizeWidth: root.sourceSizeWidth
        sourceSizeHeight: root.sourceSizeHeight
        showOverlayIcon: true
        overlayIconSource: "qrc:/qt/qml/FM/qml/assets/icons/video.svg"
        overlayIconSize: 64
        showBusyIndicator: false
        opacity: root.thumbnailReady ? 1 : 0
    }
}
