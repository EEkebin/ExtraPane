import QtQuick
// Shared shell baseline. Platforms supply Video.qml and the receiver controller.
// Linux is currently the first consumer; other platform adapters are pending.
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 1120; height: 760
    minimumWidth: 360; minimumHeight: 480
    visible: true
    title: "ExtraPane"
    color: "#10151f"
    palette.window: "#10151f"
    palette.windowText: "#edf1f8"
    palette.base: "#172130"
    palette.text: "#edf1f8"
    palette.button: "#253349"
    palette.buttonText: "#edf1f8"
    palette.highlight: "#6a9dff"
    palette.highlightedText: "#10151f"
    palette.toolTipBase: "#253349"
    palette.toolTipText: "#edf1f8"
    palette.placeholderText: "#aab8cc"
    property bool inputPaused: false
    component ToolbarButton: ToolButton {
        id: control
        font.pixelSize: 12
        padding: 8
        contentItem: Text {
            text: control.text; font: control.font; color: "#edf1f8"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: control.down ? "#45638d" : control.hovered ? "#354b69" : "#253349"
            border.color: "#516681"
        }
    }
    function pauseInput() { receiver.releaseInput(); inputPaused = true; revealToolbar() }
    function toggleInput() { receiver.releaseInput(); inputPaused = !inputPaused; revealToolbar() }
    property int page: Qt.application.arguments.indexOf("--host") >= 0 ? 0 : 1
    onPageChanged: receiver.releaseInput()
    footer: TabBar {
        visible: root.width < 700 && !root.fullscreen
        height: visible ? implicitHeight : 0
        currentIndex: root.page
        Repeater {
            model: ["Server", "Client", "Settings", "About"]
            TabButton {
                required property int index
                required property string modelData
                text: modelData
                onClicked: root.page = index
            }
        }
    }
    property int nextScale: 0
    property bool toolbarVisible: true
    property bool fullscreen: visibility === Window.FullScreen
    function toggleFullscreen() {
        receiver.releaseInput()
        if (fullscreen) showNormal(); else showFullScreen()
        revealToolbar()
    }
    function revealToolbar() { toolbarVisible = true; hideToolbar.restart() }
    Shortcut { sequence: "Ctrl+Shift+Q"; onActivated: receiver.disconnectFromServer() }
    Shortcut { sequence: "F11"; onActivated: root.toggleFullscreen() }
    Shortcut { sequence: "F10"; onActivated: root.pauseInput() }
    Shortcut { sequence: "F8"; onActivated: root.toggleInput() }
    onActiveChanged: { if (!active) receiver.releaseInput() }
    Timer {
        id: hideToolbar
        interval: 3000
        onTriggered: { if (!root.inputPaused && !settingsMenu.opened && !toolbarHover.hovered) root.toolbarVisible = false; else restart() }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 180
            visible: !root.fullscreen && root.width >= 700
            color: "#161e2b"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 10
                Label { text: "ExtraPane"; font.pixelSize: 23; font.bold: true; topPadding: 14; bottomPadding: 20 }
                Repeater {
                    model: ["Server", "Client", "Settings"]
                    Button {
                        required property int index
                        required property string modelData
                        text: modelData; Layout.fillWidth: true
                        checkable: true; checked: root.page === index
                        onClicked: root.page = index
                    }
                }
                Item { Layout.fillHeight: true }
                Label { text: receiver.platformLabel; color: "#aab8cc" }
                Button { text: "About"; Layout.fillWidth: true; onClicked: root.page = 3 }
            }
        }
        StackLayout {
            currentIndex: root.page
            Layout.fillHeight: true; Layout.fillWidth: true
            Pane {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 24; spacing: 16
                    Label { text: "Server"; font.pixelSize: 30; font.bold: true }
                    Label { text: "Accept connections from your other devices."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label {
                        text: receiver.hostAvailability.length ? receiver.hostAvailability : receiver.hostDescription
                        color: "#f0c66e"; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    ComboBox {
                        id: hostAddress; Layout.fillWidth: true; model: receiver.addresses; textRole: "label"
                        currentIndex: receiver.platformLabel === "Windows" ? 0 : receiver.addresses.length > 1 ? 1 : 0; enabled: !receiver.hosting
                    }
                    Button {
                        text: receiver.hosting ? "Stop server" : "Start server"
                        enabled: receiver.hostAvailability.length === 0
                        onClicked: { if (receiver.hosting) receiver.stopHost(); else { const selected=receiver.addresses[hostAddress.currentIndex]; receiver.startHost(selected ? selected.address : "0.0.0.0", Number(hostPort.text), requireCode.checked) } }
                    }
                    TextField { id: hostPort; text: "43824"; placeholderText: "TCP port"; validator: IntValidator { bottom: 1; top: 65535 } enabled: !receiver.hosting }
                    CheckBox { id: requireCode; text: "Require access code"; enabled: !receiver.hosting }
                    Label { text: receiver.accessCode; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Button { text: "Generate new code"; enabled: receiver.hosting && requireCode.checked; onClicked: receiver.generateAccessCode() }
                    Label { text: receiver.hostStatus; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: "Connected network addresses"; font.bold: true }
                    Repeater {
                        model: receiver.addresses
                        Label { required property var modelData; visible: modelData.address !== "0.0.0.0"; text: modelData.label }
                    }
                    Button { text: "Refresh addresses"; onClicked: receiver.refreshAddresses() }
                    Item { Layout.fillHeight: true }
                }
            }
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.fullscreen ? 0 : 24
                    spacing: root.fullscreen ? 0 : 12
                    Label { visible: !root.fullscreen; text: "Client"; font.pixelSize: 30; font.bold: true }
                    RowLayout {
                        visible: !receiver.active && !root.fullscreen
                        TextField { id: clientPort; text: "43824"; placeholderText: "TCP port"; validator: IntValidator { bottom: 1; top: 65535 } Layout.preferredWidth: 100 }
                        TextField { id: clientCode; placeholderText: "Access code (optional)"; echoMode: TextInput.Password; maximumLength: 6; Layout.fillWidth: true }
                    }
                    RowLayout {
                        visible: !receiver.active && !root.fullscreen
                        TextField {
                            id: serverAddress; Layout.fillWidth: true
                            placeholderText: "Server IPv4 address"; text: receiver.savedAddress
                            onAccepted: connectButton.clicked()
                        }
                        Button {
                            id: connectButton; text: "Connect"; enabled: receiver.resources.canConnect
                            onClicked: {
                                const selected = receiver.addresses[localAddress.currentIndex]
                                receiver.connectTo(serverAddress.text, selected ? selected.address : "0.0.0.0", inputAllowed.checked, root.nextScale, selected ? selected.index : 0, Number(clientPort.text), clientCode.text)
                                clientCode.text = ""
                                root.nextScale = 0
                                root.revealToolbar()
                            }
                        }
                    }
                    RowLayout {
                        visible: !receiver.active && !root.fullscreen
                        Label { text: "Connect through" }
                        ComboBox { id: localAddress; Layout.fillWidth: true; model: receiver.addresses; textRole: "label"; valueRole: "address" }
                        Button { text: "Refresh"; onClicked: receiver.refreshAddresses() }
                    }
                    Label {
                        visible: receiver.availability.length > 0; text: receiver.availability; color: "#f0c66e"
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Rectangle {
                        id: viewport; color: "#080b11"; Layout.fillWidth: true; Layout.fillHeight: true
                        ScrollView {
                            anchors.fill: parent; anchors.margins: 18
                            visible: !receiver.active
                            contentWidth: availableWidth
                            ResourcePicker { width: parent.width; resourceModel: receiver.resources }
                        }
                        Loader {
                            anchors.fill: parent; active: receiver.availability.length === 0; visible: receiver.active; source: "qrc:/Video.qml"
                            onLoaded: item.inputPaused = Qt.binding(function() { return root.inputPaused || root.toolbarVisible || settingsMenu.opened })
                        }
                        Item {
                            anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                            width: 96; height: 8; visible: receiver.active
                            HoverHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onHoveredChanged: { if (hovered) root.revealToolbar() } }
                        }
                        Rectangle {
                            id: toolbar
                            z: 20
                            visible: root.toolbarVisible && receiver.active
                            anchors.top: parent.top; anchors.topMargin: 8; anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(parent.width - 24, 680); height: 48; radius: 12; color: "#ee253349"
                            HoverHandler { id: toolbarHover; acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad }
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 6
                                Label { text: "ExtraPane"; color: "#edf1f8"; visible: viewport.width >= 660; leftPadding: 8; Layout.fillWidth: true }
                                ToolbarButton { text: root.inputPaused ? "Resume input" : "Pause input"; onClicked: root.toggleInput() }
                                ToolbarButton {
                                    text: "Settings"
                                    onClicked: { receiver.releaseInput(); settingsMenu.popup() }
                                    Menu {
                                        id: settingsMenu
                                        MenuItem { id: showStatistics; text: "Show statistics"; checkable: true; checked: false }
                                        MenuSeparator {}
                                        MenuItem { text: "Native on next connection"; checkable: true; checked: root.nextScale === 0; onTriggered: root.nextScale = 0 }
                                        MenuItem { text: "75% on next connection"; checkable: true; checked: root.nextScale === 1; onTriggered: root.nextScale = 1 }
                                        MenuItem { text: "50% on next connection"; checkable: true; checked: root.nextScale === 2; onTriggered: root.nextScale = 2 }
                                    }
                                }
                                ToolbarButton { text: root.fullscreen ? "Windowed" : "Fullscreen"; onClicked: root.toggleFullscreen() }
                                ToolbarButton { text: "Minimize"; onClicked: { receiver.releaseInput(); root.showMinimized() } }
                                ToolbarButton { text: "Disconnect"; onClicked: receiver.disconnectFromServer() }
                            }
                        }
                        Rectangle {
                            visible: receiver.active && showStatistics.checked
                            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 12
                            width: Math.min(parent.width - 24, 600); height: statsLabel.implicitHeight + 20
                            color: "#dd172130"; radius: 8
                            Label {
                                id: statsLabel; anchors.fill: parent; anchors.margins: 10; wrapMode: Text.WordWrap
                                text: receiver.statistics + "\nDecoder: " + receiver.decoder + "\nReceive measurements, not end-to-end latency."
                            }
                        }
                    }
                    Label { visible: !root.fullscreen; text: receiver.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { visible: !root.fullscreen; text: "F8 pauses input | Ctrl+Shift+Q disconnects · F11 toggles fullscreen · F10 pauses input and shows controls"; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            Pane {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 24; spacing: 16
                    Label { text: "Settings"; font.pixelSize: 30; font.bold: true }
                    CheckBox { id: inputAllowed; text: "Send mouse, keyboard, touch and pen input"; checked: true; enabled: !receiver.active }
                    Label { text: "Keyboard forwarding follows the server's keyboard layout. Available input features depend on both devices."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: receiver.inputDescription; color: "#f0c66e"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: receiver.rendererDescription; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: "Use a trusted LAN. Connections are encrypted, but automatic LAN mode does not verify the other computer's identity."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                }
            }
            Pane {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 24; spacing: 16
                    Label { text: "ExtraPane"; font.pixelSize: 32; font.bold: true }
                    Label { text: "Another screen for your desktop." }
                    Label { text: "Development build: " + receiver.version }
                    Button { text: "README"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane#readme") }
                    Button { text: "Releases and changelog"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane/releases") }
                    Button { text: "GitHub repository"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane") }
                    Label { text: "Third-party libraries retain their own licenses. A project-wide license has not been selected."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
