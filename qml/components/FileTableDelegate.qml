import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../style"

Item {
    id: root

    required property var controller
    required property var panel // reference to FilePanel to read column widths
    
    // Model roles
    required property int index
    required property string name
    required property string path
    required property bool isDirectory
    required property bool isSelected
    required property string sizeText
    required property string modifiedText
    required property string suffix
    property bool currentItem: false
    property bool panelActive: true
    property bool scrolling: false

    // Signals
    signal clicked(var mouse)
    signal doubleClicked()
    signal rightClicked()

    implicitHeight: Theme.rowHeight

    property bool isRenaming: false
    property real visualOffsetX: 0

    onPathChanged: {
        isRenaming = false
        visualOffsetX = 0
    }

    ListView.onPooled: {
        isRenaming = false
        visualOffsetX = 0
    }

    ListView.onReused: {
        isRenaming = false
        visualOffsetX = 0
    }

    function startRename() {
        root.isRenaming = true
    }

    Rectangle {
        id: bgRect
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        anchors.topMargin: 1
        anchors.bottomMargin: 1
        radius: 6
        
        color: isSelected
               ? (root.panelActive ? Theme.itemSelectedFill : Theme.itemSelectedFillInactive)
               : (root.currentItem
                  ? Theme.itemCurrentFill
                  : (hover.hovered ? Qt.rgba(Theme.itemHoverFill.r, Theme.itemHoverFill.g, Theme.itemHoverFill.b,
                                               Theme.itemHoverFill.a + (themeController.isDark ? 0.02 : 0.015))
                                   : "transparent"))
        border.color: isSelected
                      ? (root.panelActive ? Theme.itemSelectedBorder : Theme.itemSelectedBorderInactive)
                      : (root.currentItem ? Theme.itemCurrentBorder : "transparent")
        border.width: isSelected || root.currentItem ? 1 : 0
        transform: Translate { x: root.visualOffsetX }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: root.currentItem || isSelected ? 3 : 0
            radius: 1.5
            color: isSelected ? Theme.accent : Theme.itemCurrentBorder
            visible: width > 0
        }

        Behavior on color { ColorAnimation { duration: 90 } }
    }

    HoverHandler {
        id: hover
        enabled: !root.scrolling
        onHoveredChanged: {
            if (hovered) {
                root.controller.hoveredPath = root.path
            } else if (root.controller.hoveredPath === root.path) {
                root.controller.hoveredPath = ""
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: false 

        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.rightClicked()
            } else {
                root.clicked(mouse)
            }
        }

        onDoubleClicked: root.doubleClicked()
    }

    // Row containing all columns
    Row {
        id: rowLayout
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 0
        transform: Translate { x: root.visualOffsetX }

        // COLUMN 1: Name (Icon + Label / Textfield)
        Item {
            width: root.panel.colWidthName
            height: parent.height
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.rightMargin: 8
                spacing: 8
                visible: !root.isRenaming

                Item {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignVCenter
                    
                    Image {
                        anchors.centerIn: parent
                        source: "image://icon/" + root.path
                        sourceSize: Qt.size(20, 20)
                        asynchronous: true
                        cache: true
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.name
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    font.weight: isSelected || root.currentItem ? Font.Medium : Font.Normal
                }
            }

            Loader {
                id: renameLoader
                anchors.fill: parent
                anchors.leftMargin: 24 // Offset for icon width
                anchors.rightMargin: 8
                active: root.isRenaming
                visible: root.isRenaming
                sourceComponent: TextField {
                    text: root.name
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 13
                    color: Theme.textPrimary
                    selectByMouse: true
                    background: Rectangle { 
                        color: Theme.surface
                        radius: 4
                        border.color: Theme.accent
                    }

                    onAccepted: {
                        if (root.index >= 0) {
                            const idx = root.index
                            const txt = text
                            const ctrl = controller
                            Qt.callLater(function() {
                                if (ctrl.rename(idx, txt)) {
                                    root.isRenaming = false
                                } else {
                                    if (renameLoader.item) {
                                        renameLoader.item.forceActiveFocus()
                                        renameLoader.item.selectAll()
                                    }
                                }
                            })
                        }
                    }
                    onActiveFocusChanged: if (!activeFocus) root.isRenaming = false
                    
                    Component.onCompleted: {
                        forceActiveFocus()
                        let lastDot = name.lastIndexOf(".")
                        if (!isDirectory && lastDot > 0) {
                            select(0, lastDot)
                        } else {
                            selectAll()
                        }
                    }
                }
            }
        }

        // COLUMN 2: Size
        Item {
            width: root.panel.colWidthSize
            height: parent.height
            clip: true

            Label {
                anchors.fill: parent
                anchors.rightMargin: 12
                text: root.sizeText
                color: Theme.textSecondary
                opacity: 0.92
                font.pixelSize: 12
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // COLUMN 3: Type (visible if checked)
        Item {
            width: root.panel.colWidthType
            height: parent.height
            visible: root.panel.colShowType
            clip: true

            Label {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                text: getFileTypeString(root.suffix, root.isDirectory)
                color: Theme.textSecondary
                opacity: 0.92
                font.pixelSize: 12
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // COLUMN 4: Date Modified (visible if checked)
        Item {
            width: root.panel.colWidthDate
            height: parent.height
            visible: root.panel.colShowDate
            clip: true

            Label {
                anchors.fill: parent
                anchors.rightMargin: 8
                text: root.modifiedText
                color: Theme.textSecondary
                opacity: 0.92
                font.pixelSize: 12
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    function getFileTypeString(suffix, isDirectory) {
        if (isDirectory) return "Folder"
        if (!suffix) return "File"
        let s = suffix.toLowerCase()
        if (s === "png" || s === "jpg" || s === "jpeg" || s === "gif" || s === "webp" || s === "bmp" || s === "ico") return s.toUpperCase() + " Image"
        if (s === "pdf") return "PDF Document"
        if (s === "txt" || s === "md" || s === "ini" || s === "json" || s === "xml" || s === "cfg" || s === "log") return s.toUpperCase() + " Text Document"
        if (s === "mp3" || s === "wav" || s === "flac" || s === "ogg" || s === "m4a") return s.toUpperCase() + " Audio"
        if (s === "mp4" || s === "mkv" || s === "avi" || s === "mov") return s.toUpperCase() + " Video"
        if (s === "zip" || s === "rar" || s === "7z" || s === "tar" || s === "gz") return s.toUpperCase() + " Archive"
        if (s === "exe" || s === "msi" || s === "bat" || s === "cmd") return s.toUpperCase() + " Application"
        return s.toUpperCase() + " File"
    }
}
