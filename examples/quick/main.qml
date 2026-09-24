import QtQuick
import Noisemaker 1.0

// The program fills the window. Space pauses and resumes it; an error
// from the compiler or the renderer is shown over the bottom edge.
Rectangle {
    id: root
    property alias dataRoot: noisemaker.dataRoot
    property alias program: noisemaker.program
    color: "black"

    NoisemakerItem {
        id: noisemaker
        objectName: "noisemaker"
        anchors.fill: parent
        focus: true
        Keys.onSpacePressed: running = !running
        Accessible.role: Accessible.Animation
        Accessible.name: running ? qsTr("Noisemaker program, playing") : qsTr("Noisemaker program, paused")
    }

    Text {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
        visible: noisemaker.errorString.length > 0
        text: noisemaker.errorString
        color: "white"
        style: Text.Outline
        styleColor: "black"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
