import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import FM
import "../style"

Item {
    id: root

    implicitWidth: 360
    implicitHeight: mainContainer.height

    readonly property var operationErrorInfo: workspaceController.operationQueue.lastError || ({})
    readonly property bool hasOperationError: workspaceController.operationQueue.error.length > 0
    readonly property string operationErrorTitle: operationErrorInfo.title || "Operation failed"
    readonly property string operationErrorMessage: operationErrorInfo.message || workspaceController.operationQueue.error
    readonly property string operationErrorPath: operationErrorInfo.path || ""
    readonly property var operationErrorActions: operationErrorInfo.actions || []
    readonly property bool canRetry: operationErrorActions.indexOf("retry") >= 0
    readonly property bool canRefresh: operationErrorActions.indexOf("refresh") >= 0
    readonly property bool canCopyPath: operationErrorActions.indexOf("copyPath") >= 0 && operationErrorPath.length > 0
    readonly property bool canRestartAsAdmin: operationErrorInfo.code === "accessDenied"
                                              && operationErrorActions.indexOf("restartAsAdmin") >= 0
                                              && typeof adminController !== "undefined"
                                              && adminController
                                              && !adminController.isElevated
    property bool active: workspaceController.operationQueue.busy || workspaceController.operationQueue.error.length > 0

    visible: opacity > 0
    opacity: active ? 1.0 : 0.0
    y: active ? 0 : 20

    Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on y { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }

    Rectangle {
        id: mainContainer
        width: parent.width
        height: content.implicitHeight + 28
        radius: 18
        color: Theme.panelSurfaceStrong
        border.color: root.hasOperationError
                      ? Theme.withAlpha(Theme.danger, 0.25)
                      : Theme.panelBorder
        border.width: 1

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowBlur: 0.75
            shadowVerticalOffset: 10
            shadowOpacity: 0.35
            shadowColor: Theme.glassShadow
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 6
            radius: 3
            color: root.hasOperationError ? Theme.danger : Theme.accent
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                    Rectangle {
                        width: 40
                        height: 40
                        radius: 12
                        color: root.hasOperationError
                               ? Theme.withAlpha(Theme.danger, 0.14)
                               : Theme.withAlpha(Theme.accent, 0.14)
                        border.color: root.hasOperationError
                                      ? Theme.withAlpha(Theme.danger, 0.26)
                                      : Theme.withAlpha(Theme.accent, 0.26)
                        border.width: 1

                    Image {
                        anchors.centerIn: parent
                        source: root.hasOperationError
                                ? "../assets/icons/info.svg"
                                : "../assets/icons/refresh.svg"
                        sourceSize: Qt.size(20, 20)

                        RotationAnimation on rotation {
                            from: 0; to: 360; duration: 1800
                            loops: Animation.Infinite
                            running: workspaceController.operationQueue.busy && !root.hasOperationError
                        }

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: root.hasOperationError ? Theme.danger : Theme.accent
                        }
                    }
                }

                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true

                    Label {
                        text: root.hasOperationError ? root.operationErrorTitle : "Operations"
                        font.bold: true
                        font.pixelSize: 15
                        color: root.hasOperationError ? Theme.danger : Theme.textPrimary
                    }

                    RowLayout {
                        spacing: 6
                        visible: workspaceController.operationQueue.busy

                        Rectangle {
                            radius: 9
                            height: 20
                            implicitWidth: itemsLabel.implicitWidth + 14
                            color: Theme.panelSurface
                            border.color: Theme.panelBorder
                            border.width: 1

                            Label {
                                id: itemsLabel
                                anchors.centerIn: parent
                                text: workspaceController.operationQueue.completedItems + "/" + workspaceController.operationQueue.totalItems
                                color: Theme.textPrimary
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }

                        Rectangle {
                            visible: workspaceController.operationQueue.speedText !== ""
                            radius: 9
                            height: 20
                            implicitWidth: speedLabel.implicitWidth + 14
                            color: Theme.withAlpha(Theme.accent, 0.10)
                            border.color: Theme.withAlpha(Theme.accent, 0.18)
                            border.width: 1

                            Label {
                                id: speedLabel
                                anchors.centerIn: parent
                                text: workspaceController.operationQueue.speedText
                                color: Theme.accent
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }

                        Rectangle {
                            visible: workspaceController.operationQueue.remainingTimeText !== ""
                            radius: 9
                            height: 20
                            implicitWidth: etaLabel.implicitWidth + 14
                            color: Theme.panelSurface
                            border.color: Theme.panelBorder
                            border.width: 1

                            Label {
                                id: etaLabel
                                anchors.centerIn: parent
                                text: workspaceController.operationQueue.remainingTimeText
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: workspaceController.operationQueue.busy

                ProgressBar {
                    id: pBar
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: workspaceController.operationQueue.progress

                    background: Rectangle {
                        implicitHeight: 10
                        color: Theme.panelBorder
                        radius: 5
                        opacity: 0.35
                    }

                    contentItem: Item {
                        Rectangle {
                            width: pBar.visualPosition * parent.width
                            height: parent.height
                            radius: 5
                            color: root.hasOperationError ? Theme.danger : Theme.accent

                            Rectangle {
                                anchors.right: parent.right
                                width: 18
                                height: parent.height
                                radius: 5
                                color: "white"
                                opacity: 0.22
                                visible: pBar.visualPosition > 0.05
                            }
                        }
                    }

                    Behavior on value {
                        NumberAnimation { duration: 180 }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Rectangle {
                        radius: 9
                        height: 20
                        implicitWidth: pctLabel.implicitWidth + 14
                        color: Theme.withAlpha(Theme.accent, 0.10)
                        border.color: Theme.withAlpha(Theme.accent, 0.16)
                        border.width: 1

                        Label {
                            id: pctLabel
                            anchors.centerIn: parent
                            text: Math.round(workspaceController.operationQueue.progress * 100) + "%"
                            color: Theme.accent
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }

                    Item { Layout.preferredWidth: 8 }

                    Label {
                        text: workspaceController.operationQueue.remainingTimeText
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        visible: workspaceController.operationQueue.remainingTimeText !== ""
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: workspaceController.operationQueue.currentLabel || "Preparing..."
                        color: Theme.textPrimary
                        font.pixelSize: 11
                        font.bold: true
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: msgLabel.implicitHeight + 24
                color: root.hasOperationError
                       ? Theme.withAlpha(Theme.danger, 0.06)
                       : Theme.panelSurfaceSoft
                radius: 14
                border.color: root.hasOperationError
                              ? Theme.withAlpha(Theme.danger, 0.2)
                              : Theme.panelBorder
                border.width: 1

                Label {
                    id: msgLabel
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.hasOperationError ? root.operationErrorMessage
                                                  : (workspaceController.operationQueue.currentLabel || "Initializing...")
                    color: root.hasOperationError ? Theme.danger : Theme.textPrimary
                    font.pixelSize: 11
                    font.family: "Segoe UI Semibold, Arial"
                    wrapMode: Text.Wrap
                    elide: Text.ElideMiddle
                    maximumLineCount: 2
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    visible: root.hasOperationError && root.operationErrorPath.length > 0
                    Layout.fillWidth: true
                    text: root.operationErrorPath
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }

                Button {
                    id: retryBtn
                    visible: root.hasOperationError && root.canRetry
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    text: "Retry"

                    background: Rectangle {
                        radius: 9
                        color: retryBtn.pressed
                               ? Theme.withAlpha(Theme.accent, 0.14)
                               : (retryBtn.hovered ? Theme.withAlpha(Theme.accent, 0.08) : "transparent")
                        border.color: Theme.withAlpha(Theme.accent, 0.25)
                        border.width: 1
                    }

                    contentItem: Label {
                        text: retryBtn.text
                        color: Theme.accent
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        workspaceController.operationQueue.retryLastOperation()
                    }
                }

                Button {
                    id: refreshBtn
                    visible: root.hasOperationError && root.canRefresh
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    text: "Refresh"

                    background: Rectangle {
                        radius: 9
                        color: refreshBtn.pressed
                               ? Theme.withAlpha(Theme.accent, 0.14)
                               : (refreshBtn.hovered ? Theme.withAlpha(Theme.accent, 0.08) : "transparent")
                        border.color: Theme.withAlpha(Theme.accent, 0.25)
                        border.width: 1
                    }

                    contentItem: Label {
                        text: refreshBtn.text
                        color: Theme.accent
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        const window = root.Window.window
                        workspaceController.operationQueue.clearError()
                        if (window && window.refreshActivePanel) {
                            window.refreshActivePanel()
                        }
                    }
                }

                Button {
                    id: copyPathBtn
                    visible: root.hasOperationError && root.canCopyPath
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    text: "Copy path"

                    background: Rectangle {
                        radius: 9
                        color: copyPathBtn.pressed
                               ? Theme.withAlpha(Theme.accent, 0.14)
                               : (copyPathBtn.hovered ? Theme.withAlpha(Theme.accent, 0.08) : "transparent")
                        border.color: Theme.withAlpha(Theme.accent, 0.25)
                        border.width: 1
                    }

                    contentItem: Label {
                        text: copyPathBtn.text
                        color: Theme.accent
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        workspaceController.copyTextToClipboard(root.operationErrorPath)
                    }
                }

                Button {
                    id: adminBtn
                    visible: root.hasOperationError && root.canRestartAsAdmin
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    text: "Run as admin"

                    background: Rectangle {
                        radius: 9
                        color: adminBtn.pressed
                               ? Theme.withAlpha(Theme.warning, 0.16)
                               : (adminBtn.hovered ? Theme.withAlpha(Theme.warning, 0.10) : "transparent")
                        border.color: Theme.withAlpha(Theme.warning, 0.28)
                        border.width: 1
                    }

                    contentItem: Label {
                        text: adminBtn.text
                        color: Theme.warning
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        const window = root.Window.window
                        if (window && window.relaunchAsAdmin) {
                            window.relaunchAsAdmin()
                        }
                    }
                }

                Button {
                    id: cancelBtn
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    text: root.hasOperationError ? "Dismiss" : "Cancel operation"

                    background: Rectangle {
                        radius: 9
                        color: cancelBtn.pressed
                               ? Theme.withAlpha(Theme.danger, 0.14)
                               : (cancelBtn.hovered ? Theme.withAlpha(Theme.danger, 0.08) : "transparent")
                        border.color: root.hasOperationError
                                      ? Theme.panelBorder
                                      : Theme.withAlpha(Theme.danger, 0.3)
                        border.width: 1
                    }

                    contentItem: Label {
                        text: cancelBtn.text
                        color: root.hasOperationError ? Theme.textPrimary : Theme.danger
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        if (root.hasOperationError) {
                            workspaceController.operationQueue.clearError()
                        } else {
                            workspaceController.operationQueue.cancel()
                        }
                    }
                }
            }
        }
    }
}
