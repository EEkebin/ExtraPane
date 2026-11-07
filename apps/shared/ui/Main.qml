import QtQuick
// Shared shell baseline. Platforms supply Video.qml and the receiver controller.
// Windows and Linux supply native platform controllers.
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 1120; height: 760
    minimumWidth: 360; minimumHeight: 480
    visible: true
    title: "ExtraPane"
    font.family: windowsClient ? "Segoe UI" : "sans-serif"
    font.pixelSize: 14
    Material.theme: Material.Dark
    Material.accent: "#79b2ff"
    Material.primary: "#15233b"
    Material.background: "#0e1727"
    Material.foreground: "#e4ecfa"
    color: "#0e1727"
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
    readonly property bool multiClient: windowsClient && typeof receiver.connectionRuntime !== "undefined"
    property bool windowsClient: receiver.platformLabel === "Windows"
    property bool sidebarCollapsed: false
    property bool inputPaused: false
    component ToolbarButton: ToolButton {
        id: control
        property bool destructive: false
        font.pixelSize: 12
        padding: 8
        contentItem: Text {
            text: control.text; font: control.font; color: "#edf1f8"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: control.destructive ? (control.down ? "#8f1d2a" : control.hovered ? "#ce3949" : "#b32635") : control.down ? "#45638d" : control.hovered ? "#354b69" : "#253349"
            border.color: "#516681"
        }
    }
    function pauseInput() { receiver.releaseInput(); inputPaused = true; revealToolbar() }
    function toggleInput() { receiver.releaseInput(); inputPaused = !inputPaused; revealToolbar() }
    property int page: Qt.application.arguments.indexOf("--client") >= 0 ? 1 : 0
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
    property bool fullscreen: !windowsClient && visibility === Window.FullScreen
    function toggleFullscreen() {
        if (windowsClient) return
        receiver.releaseInput()
        if (fullscreen) showNormal(); else showFullScreen()
        revealToolbar()
    }
    function revealToolbar() { toolbarVisible = true; hideToolbar.restart() }
    Shortcut { sequence: "Ctrl+Shift+Q"; onActivated: receiver.disconnectFromServer() }
    Shortcut { sequence: "F11"; onActivated: root.toggleFullscreen() }
    Shortcut { enabled: !root.windowsClient; sequence: "F10"; onActivated: root.pauseInput() }
    Shortcut { enabled: !root.windowsClient; sequence: "F8"; onActivated: root.toggleInput() }
    onActiveChanged: { if (!active) receiver.releaseInput() }
    Timer {
        id: hideToolbar
        interval: 3000
        onTriggered: { if (!root.inputPaused && !settingsMenu.opened && !toolbarHover.hovered) root.toolbarVisible = false; else restart() }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            id: sidebar
            objectName: "navigationSidebar"
            Layout.fillHeight: true
            Layout.preferredWidth: root.sidebarCollapsed ? 72 : 224
            visible: !root.fullscreen && root.width >= 700
            color: "#0a1120"
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: "#1a2940" }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 64
                    Image { id: brandIcon; visible: !root.sidebarCollapsed; source: "qrc:/extrapane.png"; width: 30; height: 30; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; fillMode: Image.PreserveAspectFit }
                    Label { visible: !root.sidebarCollapsed; text: "ExtraPane"; font.pixelSize: 20; font.weight: Font.DemiBold; anchors.left: brandIcon.right; anchors.leftMargin: 6; anchors.verticalCenter: parent.verticalCenter }
                    IconButton { objectName: "sidebarToggle"; symbol: "menu"; description: root.sidebarCollapsed ? "Expand navigation" : "Collapse navigation"; x: root.sidebarCollapsed ? (parent.width - width) / 2 : parent.width - width; anchors.verticalCenter: parent.verticalCenter; onClicked: root.sidebarCollapsed = !root.sidebarCollapsed }
                }
                Repeater {
                    model: [{name: "Server", icon: "server"}, {name: "Client", icon: "client"}, {name: "Settings", icon: "settings"}]
                    ToolButton {
                        id: navigation
                        required property int index
                        required property var modelData
                        objectName: "navigation_" + modelData.icon
                        Layout.fillWidth: true; Layout.preferredHeight: 48
                        Accessible.name: modelData.name
                        ToolTip.visible: root.sidebarCollapsed && hovered
                        ToolTip.text: modelData.name; ToolTip.delay: 400
                        onClicked: root.page = index
                        padding: 0
                        contentItem: Item {
                            AppIcon { id: navIcon; symbol: navigation.modelData.icon; stroke: root.page === navigation.index ? "#9ac4ff" : "#8fa2bf"; width: 22; height: 22; x: root.sidebarCollapsed ? (parent.width - width) / 2 : 14; anchors.verticalCenter: parent.verticalCenter }
                            Label { visible: !root.sidebarCollapsed; text: navigation.modelData.name; color: root.page === navigation.index ? "#eaf2ff" : "#aabbd3"; font.weight: root.page === navigation.index ? Font.DemiBold : Font.Normal; anchors.left: navIcon.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter }
                        }
                        background: Rectangle {
                            radius: 10
                            color: root.page === navigation.index ? "#192e4b" : navigation.hovered ? "#132136" : "transparent"
                            Rectangle { visible: root.page === navigation.index; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 20; radius: 2; color: "#79b2ff" }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Label { visible: !root.sidebarCollapsed; text: receiver.platformLabel.toUpperCase(); font.pixelSize: 10; font.letterSpacing: 1.5; color: "#7287a4"; leftPadding: 14; bottomPadding: 8 }
                ToolButton {
                    id: aboutNavigation
                    objectName: "navigation_about"
                    Layout.fillWidth: true; Layout.preferredHeight: 48
                    Accessible.name: "About"
                    ToolTip.visible: root.sidebarCollapsed && hovered; ToolTip.text: "About"
                    onClicked: root.page = 3
                    padding: 0
                    contentItem: Item {
                        AppIcon { id: aboutIcon; symbol: "about"; width: 22; height: 22; x: root.sidebarCollapsed ? (parent.width - width) / 2 : 14; anchors.verticalCenter: parent.verticalCenter; stroke: root.page === 3 ? "#9ac4ff" : "#8fa2bf" }
                        Label { visible: !root.sidebarCollapsed; text: "About"; color: "#aabbd3"; anchors.left: aboutIcon.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter }
                    }
                    background: Rectangle { radius: 10; color: root.page === 3 ? "#192e4b" : aboutNavigation.hovered ? "#132136" : "transparent" }
                }
            }
        }
        StackLayout {
            currentIndex: root.page
            Layout.fillHeight: true; Layout.fillWidth: true
            Pane {
                objectName: "serverPage"
                ScrollView {
                    id: serverScroll
                    anchors.fill: parent; anchors.margins: root.width < 700 ? 16 : 32
                    // Keep a stable gutter even while the scrollbar is faded out.
                    rightPadding: ScrollBar.vertical.implicitWidth + 12
                    contentWidth: availableWidth
                ColumnLayout {
                    width: serverScroll.availableWidth; spacing: 16
                    Label { text: "Server"; font.pixelSize: 30; font.weight: Font.DemiBold }
                    Label { text: "Accept connections from your other devices."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label {
                        text: receiver.hostAvailability.length ? receiver.hostAvailability : receiver.hostDescription
                        color: receiver.hostAvailability.length ? "#f0c66e" : "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    ComboBox {
                        id: hostAddress; objectName: "hostAddressPicker"; Layout.fillWidth: true; model: receiver.addresses; textRole: "label"
                        currentIndex: receiver.platformLabel === "Windows" ? 0 : receiver.addresses.length > 1 ? 1 : 0; enabled: !receiver.hosting && (receiver.platformLabel !== "Windows" || !receiver.cameraSetupRunning)
                    }
                    Button {
                        objectName: "serverStartButton"
                        highlighted: true
                        leftPadding: 22; rightPadding: 22
                        background: Rectangle { implicitHeight: 44; radius: 10; color: parent.enabled ? (receiver.hosting ? (parent.down ? "#8f1d2a" : parent.hovered ? "#ce3949" : "#b32635") : (parent.down ? "#3468a7" : parent.hovered ? "#5288cb" : "#3e76bb")) : "#27364c"; border.width: parent.activeFocus ? 2 : 0; border.color: "#a7ceff" }
                        contentItem: Text { text: parent.text; font: parent.font; color: parent.enabled ? "#ffffff" : "#8fa0b6"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        text: receiver.hosting ? "Stop server" : receiver.platformLabel === "Windows" && receiver.cameraSetupRunning ? "Preparing server..." : "Start server"
                        enabled: receiver.hosting || (receiver.hostAvailability.length === 0 && (receiver.platformLabel !== "Windows" || !receiver.cameraSetupRunning))
                        onClicked: { if (receiver.hosting) receiver.stopHost(); else { const selected=receiver.addresses[hostAddress.currentIndex]; receiver.startHost(selected ? selected.address : "0.0.0.0", Number(hostPort.text), requireCode.checked) } }
                    }
                    Repeater {
                        objectName: "hostComponentsRepeater"
                        model: receiver.platformLabel === "Windows" ? receiver.hostComponents : []
                        Label {
                            required property var modelData
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            objectName: "serverComponent_" + modelData.label
                            visible: !modelData.available && modelData.detail.length > 0
                            text: modelData.label + ": " + modelData.detail
                            color: "#f0c66e"
                        }
                    }
                    Button { visible: receiver.platformLabel === "Windows"; text: "Eject USB"; enabled: receiver.hosting; onClicked: receiver.ejectUsb() }
                    TextField { id: hostPort; text: "43824"; placeholderText: "TCP port"; validator: IntValidator { bottom: 1; top: 65535 } enabled: !receiver.hosting && (receiver.platformLabel !== "Windows" || !receiver.cameraSetupRunning) }
                    CheckBox { id: requireCode; text: "Require access code"; enabled: !receiver.hosting && (receiver.platformLabel !== "Windows" || !receiver.cameraSetupRunning) }
                    Label { text: receiver.accessCode; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Button { text: "Generate new code"; enabled: receiver.hosting && requireCode.checked; onClicked: receiver.generateAccessCode() }
                    Label { text: receiver.hostStatus; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: "Connected network addresses"; font.bold: true }
                    Repeater {
                        model: receiver.addresses
                        Label { required property var modelData; visible: modelData.address !== "0.0.0.0"; text: modelData.label }
                    }
                    Button { text: "Refresh addresses"; onClicked: receiver.refreshAddresses() }
                }
                }
            }
            Item {
                objectName: "clientPage"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.fullscreen ? 0 : root.width < 700 ? 16 : 32
                    spacing: root.fullscreen ? 0 : 12
                    Label { visible: !root.fullscreen; text: "Client"; font.pixelSize: 30; font.weight: Font.DemiBold }
                    RowLayout {
                        visible: !root.multiClient && !receiver.active && !root.fullscreen
                        TextField { id: clientPort; text: "43824"; placeholderText: "TCP port"; validator: IntValidator { bottom: 1; top: 65535 } Layout.preferredWidth: 100 }
                        TextField { id: clientCode; placeholderText: "Access code (optional)"; echoMode: TextInput.Password; maximumLength: 6; Layout.fillWidth: true }
                    }
                    RowLayout {
                        visible: !root.multiClient && !receiver.active && !root.fullscreen
                        TextField {
                            id: serverAddress; Layout.fillWidth: true
                            placeholderText: "Server IPv4 address"; text: receiver.savedAddress
                            onAccepted: connectButton.clicked()
                        }
                        Button {
                            id: connectButton; objectName: "connectButton"; text: "Connect"
                            enabled: !receiver.active && (root.windowsClient ? receiver.clientReady : receiver.availability.length === 0 && receiver.resources.canConnect)
                            onClicked: {
                                const selected = receiver.addresses[localAddress.currentIndex]
                                receiver.connectTo(serverAddress.text, selected ? selected.address : "0.0.0.0", globalInputAllowed.checked, root.nextScale, selected ? selected.index : 0, Number(clientPort.text), clientCode.text)
                                clientCode.text = ""
                                root.nextScale = 0
                                root.revealToolbar()
                            }
                        }
                    }
                    RowLayout {
                        visible: !root.multiClient && !receiver.active && !root.fullscreen
                        Label { text: "Connect through" }
                        ComboBox { id: localAddress; Layout.fillWidth: true; model: receiver.addresses; textRole: "label"; valueRole: "address" }
                        Button { text: "Refresh"; onClicked: receiver.refreshAddresses() }
                    }
                    Label {
                        visible: receiver.availability.length > 0; text: receiver.availability; color: "#f0c66e"
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Button {
                        objectName: "clientDisconnectButton"
                        visible: root.windowsClient && !root.multiClient && receiver.active
                        text: "Disconnect all"
                        background: Rectangle { implicitHeight: 44; radius: 10; color: !parent.enabled ? "#27364c" : parent.down ? "#8f1d2a" : parent.hovered ? "#ce3949" : "#b32635"; border.width: parent.activeFocus ? 2 : 0; border.color: "#ff9da8" }
                        Material.foreground: "#ffffff"
                        onClicked: receiver.disconnectFromServer()
                    }
                    Loader {
                        active: root.multiClient
                        visible: active
                        Layout.fillWidth: true; Layout.fillHeight: true
                        sourceComponent: Component { ClientConnections { controller: receiver; runtime: receiver.connectionRuntime; addresses: receiver.addresses; inputAllowed: globalInputAllowed.checked } }
                    }
                    ScrollView {
                        id: resourceScroll
                        visible: root.windowsClient && !root.multiClient
                        Layout.fillWidth: true; Layout.fillHeight: true
                        // Keep a stable gutter even while the scrollbar is faded out.
                        rightPadding: ScrollBar.vertical.implicitWidth + 12
                        contentWidth: availableWidth
                        Loader {
                            width: resourceScroll.availableWidth
                            active: root.windowsClient && !root.multiClient
                            sourceComponent: Component {
                                ResourcePicker { resourceModel: receiver.resources; controller: receiver }
                            }
                        }
                    }
                    Rectangle {
                        id: viewport; visible: !root.windowsClient; color: "#080b11"; Layout.fillWidth: true; Layout.fillHeight: true
                        ScrollView {
                            id: linuxResourceScroll
                            anchors.fill: parent; anchors.margins: 18
                            visible: !receiver.active
                            // Keep a stable gutter even while the scrollbar is faded out.
                            rightPadding: ScrollBar.vertical.implicitWidth + 12
                            contentWidth: availableWidth
                            Loader {
                                width: linuxResourceScroll.availableWidth
                                active: !root.windowsClient
                                sourceComponent: Component {
                                    ResourcePicker { resourceModel: receiver.resources }
                                }
                            }
                        }
                        Loader {
                            objectName: "clientVideoLoader"
                            anchors.fill: parent; active: !root.windowsClient && receiver.availability.length === 0; visible: receiver.active; source: "qrc:/Video.qml"
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
                                ToolbarButton { text: "Disconnect"; destructive: true; onClicked: receiver.disconnectFromServer() }
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
                    Label { visible: !root.fullscreen && !root.multiClient; text: receiver.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { visible: !root.fullscreen; text: root.windowsClient ? "Ctrl+Shift+Q disconnects all selected resources. Display opens in its own native window." : "F8 pauses input | Ctrl+Shift+Q disconnects | F11 toggles fullscreen | F10 pauses input and shows controls"; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            Pane {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: root.width < 700 ? 16 : 32; spacing: 16
                    Label { text: "Settings"; font.pixelSize: 30; font.weight: Font.DemiBold }
                    CheckBox { id: globalInputAllowed; text: "Send mouse, keyboard, touch and pen input"; checked: true; enabled: !receiver.active }
                    Label { text: "Keyboard forwarding follows the server's keyboard layout. Available input features depend on both devices."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: receiver.inputDescription; color: "#f0c66e"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: receiver.rendererDescription; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: "Use a trusted LAN. Connections are encrypted, but automatic LAN mode does not verify the other computer's identity."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                }
            }
            Pane {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: root.width < 700 ? 16 : 32; spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "ExtraPane"; font.pixelSize: 32; font.bold: true; Layout.fillWidth: true }
                        Image {
                            source: "qrc:/extrapane.png"
                            Layout.preferredWidth: 72; Layout.preferredHeight: 72
                            Layout.alignment: Qt.AlignRight | Qt.AlignTop
                            fillMode: Image.PreserveAspectFit
                            Accessible.name: "ExtraPane logo"
                        }
                    }
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
