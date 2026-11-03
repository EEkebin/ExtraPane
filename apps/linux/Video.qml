import QtQuick
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0
import SideScreen.Native 1.0

Item {
    GstGLQt6VideoItem { id: video; anchors.fill: parent }
    InputSurface {
        id: input
        anchors.fill: parent
        enabled: receiver.active
        focus: receiver.active
        sourceWidth: receiver.sourceWidth
        sourceHeight: receiver.sourceHeight
    }
    Component.onCompleted: receiver.attachSurface(video, input)
}
