import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../style"

Item {
    id: root

    property var workspaceController
    property string currentPath: ""

    readonly property bool readOnlyDestination: {
        if (!root.currentPath) return false
        if (root.currentPath.toLowerCase().startsWith("archive://")) return true
        return root.workspaceController && root.workspaceController.isInsideManagedIsoMount(root.currentPath)
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        enabled: !root.readOnlyDestination
        onDropped: (drop) => {
            if (drop.hasText) {
                const paths = [drop.text]
                root.workspaceController.operationQueue.copyTo(paths, root.currentPath)
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Theme.innerRadius(Theme.panelRadius, 1)
            color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.075 : 0.055)
            opacity: parent.containsDrag ? 1 : 0
            visible: parent.containsDrag
            border.color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.46 : 0.36)
            border.width: 1
            antialiasing: true
        }
    }
}
