import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../style"

Rectangle {
    id: headerRoot

    required property var controller
    required property var panel // reference to FilePanel to update widths

    height: 32
    color: Theme.isDark ? Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.45) 
                        : Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0.6)

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    // Context menu for toggling columns
    Menu {
        id: columnContextMenu
        
        MenuItem {
            text: "Type"
            checkable: true
            checked: headerRoot.panel.colShowType
            onTriggered: headerRoot.panel.colShowType = !headerRoot.panel.colShowType
        }
        MenuItem {
            text: "Date Modified"
            checkable: true
            checked: headerRoot.panel.colShowDate
            onTriggered: headerRoot.panel.colShowDate = !headerRoot.panel.colShowDate
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                columnContextMenu.popup(mouse.x, mouse.y)
            }
        }
    }

    Row {
        id: headerRow
        anchors.fill: parent
        spacing: 0

        // COLUMN: Name
        Item {
            width: headerRoot.panel.colWidthName
            height: parent.height
            clip: true

            HeaderButton {
                anchors.fill: parent
                text: "Name"
                active: headerRoot.controller.directoryModel.sortRole === 0
                sortOrder: headerRoot.controller.directoryModel.sortOrder
                onClicked: headerRoot.setSort(0)
            }
        }

        // RESIZE HANDLE 1
        ResizeHandle {
            onPositionChanged: (mouse) => {
                let xInHeader = mapToItem(headerRow, mouse.x, 0).x
                let newWidth = xInHeader
                if (newWidth < 150) newWidth = 150
                headerRoot.panel.colWidthName = newWidth
            }
        }

        // COLUMN: Size
        Item {
            width: headerRoot.panel.colWidthSize
            height: parent.height
            clip: true

            HeaderButton {
                anchors.fill: parent
                text: "Size"
                alignRight: true
                active: headerRoot.controller.directoryModel.sortRole === 1
                sortOrder: headerRoot.controller.directoryModel.sortOrder
                onClicked: headerRoot.setSort(1)
            }
        }

        // RESIZE HANDLE 2
        ResizeHandle {
            onPositionChanged: (mouse) => {
                let xInHeader = mapToItem(headerRow, mouse.x, 0).x
                let newWidth = xInHeader - headerRoot.panel.colWidthName
                if (newWidth < 60) newWidth = 60
                if (newWidth > 180) newWidth = 180
                headerRoot.panel.colWidthSize = newWidth
            }
        }

        // COLUMN: Type
        Item {
            width: headerRoot.panel.colWidthType
            height: parent.height
            visible: headerRoot.panel.colShowType
            clip: true

            HeaderButton {
                anchors.fill: parent
                text: "Type"
                active: headerRoot.controller.directoryModel.sortRole === 2
                sortOrder: headerRoot.controller.directoryModel.sortOrder
                onClicked: headerRoot.setSort(2)
            }
        }

        // RESIZE HANDLE 3
        ResizeHandle {
            visible: headerRoot.panel.colShowType
            onPositionChanged: (mouse) => {
                let xInHeader = mapToItem(headerRow, mouse.x, 0).x
                let newWidth = xInHeader - (headerRoot.panel.colWidthName + headerRoot.panel.colWidthSize)
                if (newWidth < 80) newWidth = 80
                if (newWidth > 250) newWidth = 250
                headerRoot.panel.colWidthType = newWidth
            }
        }

        // COLUMN: Date Modified
        Item {
            width: headerRoot.panel.colWidthDate
            height: parent.height
            visible: headerRoot.panel.colShowDate
            clip: true

            HeaderButton {
                anchors.fill: parent
                text: "Date Modified"
                active: headerRoot.controller.directoryModel.sortRole === 3
                sortOrder: headerRoot.controller.directoryModel.sortOrder
                onClicked: headerRoot.setSort(3)
            }
        }
    }

    function setSort(role) {
        let model = headerRoot.controller.directoryModel
        if (model.sortRole === role) {
            model.sortOrder = (model.sortOrder === Qt.AscendingOrder ? Qt.DescendingOrder : Qt.AscendingOrder)
        } else {
            model.sortRole = role
            model.sortOrder = Qt.AscendingOrder
        }
    }

    // Helper components inside this file
    component HeaderButton : Item {
        id: btn
        property string text: ""
        property bool active: false
        property int sortOrder: Qt.AscendingOrder
        property bool alignRight: false
        signal clicked()

        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            onClicked: btn.clicked()
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    columnContextMenu.popup(mouse.x, mouse.y)
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            color: ma.pressed ? Theme.surfaceActive : (ma.containsMouse ? Theme.surfaceHover : "transparent")
            Behavior on color { ColorAnimation { duration: 80 } }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: btn.alignRight ? 4 : 12
            anchors.rightMargin: btn.alignRight ? 12 : 12
            spacing: 4
            layoutDirection: btn.alignRight ? Qt.RightToLeft : Qt.LeftToRight

            Text {
                text: btn.text
                color: btn.active ? Theme.accent : Theme.textSecondary
                font.pixelSize: 11
                font.weight: btn.active ? Font.DemiBold : Font.Normal
                Layout.fillWidth: true
                horizontalAlignment: btn.alignRight ? Text.AlignRight : Text.AlignLeft
                elide: Text.ElideRight
            }

            Text {
                text: btn.sortOrder === Qt.AscendingOrder ? "↑" : "↓"
                color: Theme.accent
                font.pixelSize: 11
                font.bold: true
                visible: btn.active
                Layout.preferredWidth: implicitWidth
            }
        }
    }

    component ResizeHandle : Item {
        width: 8
        height: parent.height
        z: 10

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            hoverEnabled: true
            
            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: parent.height - 10
                color: parent.containsMouse ? Theme.accent : "transparent"
                opacity: 0.8
            }

            // Keep event accepted so dragging works smoothly
            onPositionChanged: (mouse) => {
                if (pressed) {
                    parent.positionChanged(mouse)
                }
            }
        }

        signal positionChanged(var mouse)
    }
}
