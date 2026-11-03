import QtQuick
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0
import ExtraPane.Native 1.0

Item {
    property bool inputPaused: false
    GstGLQt6VideoItem { id: video; anchors.fill: parent }
    InputSurface {
        id: input
        anchors.fill: parent
        enabled: receiver.active && !parent.inputPaused
        focus: enabled
        onEnabledChanged: { if (!enabled) receiver.releaseInput() }
        sourceWidth: receiver.sourceWidth
        sourceHeight: receiver.sourceHeight
    }
    Component.onCompleted: receiver.attachSurface(video, input)
}
