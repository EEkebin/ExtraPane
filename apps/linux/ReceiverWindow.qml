import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: window
    width: 1000; height: 650; minimumWidth: 360; minimumHeight: 240
    visible: true; title: "ExtraPane display"; color: "#080b11"
    Material.theme: Material.Dark
    property bool paused: false
    property bool controls: true
    readonly property bool fullscreen: visibility === Window.FullScreen
    function reveal() { controls = true; hideControls.restart() }
    function toggleFullscreen() { receiver.releaseInput(); if (fullscreen) showNormal(); else showFullScreen(); reveal() }
    onActiveChanged: { if (!active) receiver.releaseInput() }
    onClosing: event => { receiver.disconnectFromServer(); event.accepted = false }
    Shortcut { sequence: "Ctrl+Shift+Q"; onActivated: receiver.disconnectFromServer() }
    Shortcut { sequence: "F11"; onActivated: window.toggleFullscreen() }
    Shortcut { sequence: "F10"; onActivated: window.reveal() }
    Shortcut { sequence: "F8"; onActivated: { window.paused = !window.paused; receiver.releaseInput(); window.reveal() } }
    Timer { id: hideControls; interval: 3500; onTriggered: { if (!hover.hovered && !settings.opened) window.controls = false; else restart() } }
    Video {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height * receiver.sourceWidth / Math.max(1, receiver.sourceHeight))
        height: width * receiver.sourceHeight / Math.max(1, receiver.sourceWidth)
        inputPaused: window.paused || settings.opened
    }
    Label { anchors.centerIn: parent; visible: receiver.status !== "Connected"; text: receiver.status; color: "#e4ecfa"; width: parent.width - 40; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
    Button { anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; visible: !window.controls; text: "Controls"; onClicked: window.reveal() }
    Rectangle {
        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 8
        width: Math.min(parent.width - 16, 640); height: bar.implicitHeight + 12; radius: 12; color: "#ed20324d"
        visible: window.controls
        HoverHandler { id: hover; onHoveredChanged: { if (hovered) window.reveal() } }
        Flow {
            id: bar; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 6; y: 6; spacing: 4
            Button { text: "Settings"; onClicked: settings.popup(); Menu { id: settings; MenuItem { id: stats; text: "Show statistics"; checkable: true } MenuItem { text: window.paused ? "Resume input" : "Pause input"; onTriggered: { window.paused = !window.paused; receiver.releaseInput() } } } }
            Button { objectName: "displayFullscreen"; text: window.fullscreen ? "Windowed" : "Fullscreen"; onClicked: window.toggleFullscreen() }
            Button { text: "Minimize"; onClicked: { receiver.releaseInput(); window.showMinimized() } }
            Button { objectName: "displayDisconnect"; text: "Disconnect"; background: Rectangle { implicitHeight: 40; implicitWidth: 100; radius: 8; color: parent.down ? "#8f1d2a" : "#b32635" } onClicked: receiver.disconnectFromServer() }
        }
    }
    Label {
        visible: stats.checked; anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
        width: parent.width - 24; wrapMode: Text.WordWrap; padding: 10; color: "#e4ecfa"
        background: Rectangle { color: "#dd172130"; radius: 8 }
        text: receiver.statistics + "\nDecoder: " + receiver.decoder
    }
    Component.onCompleted: hideControls.restart()
}
