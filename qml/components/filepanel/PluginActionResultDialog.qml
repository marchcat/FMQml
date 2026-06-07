import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../dialogs"
import "../../style"

Dialog {
    id: root

    property string messageText: ""

    function showResult(result) {
        root.title = String(result.title || "Plugin Action")
        root.messageText = String(result.message || "Action completed.")
        root.open()
    }

    modal: true
    focus: true
    parent: Overlay.overlay
    width: Math.min(420, Math.max(280, (parent ? parent.width : 420) - 64))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: DialogShell {}

    contentItem: ColumnLayout {
        spacing: 14
        focus: true

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
                root.close()
                event.accepted = true
            }
        }

        DialogHeader {
            Layout.fillWidth: true
            iconSource: "qrc:/qt/qml/FM/qml/assets/icons/info.svg"
            iconTint: Theme.accent
            accentColor: Theme.accent
            title: root.title
            subtitle: "Plugin action"
            showCloseButton: false
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            text: root.messageText
            color: Theme.textPrimary
            font.pixelSize: 13
            wrapMode: Text.WrapAnywhere
        }

        DialogFooter {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            DialogActionButton {
                text: "OK"
                highlighted: true
                primaryColor: Theme.accent
                primaryHoverColor: Theme.accent
                primaryPressedColor: Theme.accent
                onClicked: root.close()
            }
        }
    }
}
