import "../style"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "common"

DialogSection {
    required property var editor

    title: "SAVE TARGET"
    accentColor: Theme.categoryInfo
    fillColor: editor.sectionFill
    borderColor: editor.sectionBorder

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            text: "Suggested library folder"
            font.pixelSize: Theme.fontSizeCaption
            color: Theme.textSecondary
        }

        Label {
            text: themeController.customThemeDirectory().length > 0 ? themeController.customThemeDirectory() : "Theme library folder is not available."
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeCaption
            color: Theme.textPrimary
        }

        Label {
            text: "Saved files from this folder will appear in the theme picker."
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeCaption
            color: Theme.textSecondary
        }

    }

}
