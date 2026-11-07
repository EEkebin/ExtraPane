import QtQuick
import QtQuick.Controls
Item {
    property bool inputPaused: false
    Label {
        anchors.centerIn: parent
        width: parent.width - 40
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: "Your display opens in its native receiver window. Use that window's toolbar for fullscreen, input and statistics."
    }
}
