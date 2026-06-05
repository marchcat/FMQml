import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../style"

Item {
    id: root

    property bool panelActive: false
    default property alias contentData: body.data

    implicitWidth: 100
    implicitHeight: 100

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        color: "transparent"
        radius: Theme.innerRadius(Theme.panelRadius, 1)
        clip: true
        antialiasing: true

        Item {
            id: body
            anchors.fill: parent
        }
    }
}
