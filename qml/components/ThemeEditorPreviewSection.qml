import "../style"
import QtQuick
import QtQuick.Layouts
import "common"

DialogSection {
    id: sectionRoot

    required property var editor

    title: "PREVIEW"
    accentColor: editor.dialogAccent
    fillColor: editor.sectionFill
    borderColor: editor.sectionBorder
    expandContent: true

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 8

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 6

            ThemeEditorPreviewMarkerChip {
                title: editor.hoveredTokenKey.length > 0 ? editor.hoveredTokenTitle() : "Focused token"
                detail: editor.hoveredTokenKey.length > 0 ? editor.hoveredTokenDetail() : "No token focused"
                accent: editor.hoveredTokenKey.length > 0 ? editor.tokenAreaAccent(editor.hoveredTokenKey) : editor.dialogAccent
                emphasized: editor.hoveredTokenKey.length > 0
                compact: editor.compactLayout
            }

            ThemeEditorPreviewMarkerChip {
                title: editor.hoveredTokenKey.length > 0 ? editor.tokenAreaTitle(editor.hoveredTokenKey) : "Preview area"
                detail: editor.hoveredTokenKey.length > 0 ? editor.areaMarkerDetails(editor.tokenArea(editor.hoveredTokenKey)) : "Token rows map to preview regions"
                accent: editor.hoveredTokenKey.length > 0 ? editor.tokenAreaAccent(editor.hoveredTokenKey) : Theme.categoryInfo
                emphasized: editor.hoveredTokenKey.length > 0
                compact: editor.compactLayout
            }

            ThemeEditorPreviewMarkerChip {
                title: editor.changedTokenCount() > 0 ? (editor.changedTokenCount() + " changed") : "Clean draft"
                detail: editor.changedTokenCount() > 0 ? "Changed rows expose reset controls" : "No edited colors"
                accent: editor.changedTokenCount() > 0 ? editor.dialogAccent : Theme.textSecondary
                emphasized: editor.changedTokenCount() > 0
                compact: editor.compactLayout
            }

        }

        ThemeEditorPreviewCard {
            editor: sectionRoot.editor
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 320
        }

    }

}
