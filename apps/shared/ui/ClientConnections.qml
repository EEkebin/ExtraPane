pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: panel
    required property var runtime
    property var controller: null
    required property var addresses
    property bool inputAllowed: true
    // Keep a stable gutter even while the scrollbar is faded out.
    rightPadding: ScrollBar.vertical.implicitWidth + 12
    contentWidth: availableWidth
    component DestinationFields: ColumnLayout {
        id: fields
        required property var destination
        required property var addresses
        property alias accessCode: code.text
        property var commit
        property bool valid: true
        function save() {
            const local = addresses[route.currentIndex]
            valid = commit({address: host.text, port: Number(port.text), localAddress: local ? local.address : "0.0.0.0", interfaceIndex: local ? local.index : 0})
            return valid
        }
        Label { visible: !fields.valid; text: "Enter a valid IPv4 address and port."; color: "#f0c66e" }
        RowLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            TextField { id: host; Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: "Server IPv4 address"; text: fields.destination.address; onEditingFinished: fields.save() }
            TextField { id: port; Layout.preferredWidth: 100; placeholderText: "Port"; text: fields.destination.port; validator: IntValidator { bottom: 1; top: 65535 } onEditingFinished: fields.save() }
        }
        GridLayout {
            columns: fields.width < 480 ? 1 : 2
            Layout.fillWidth: true; Layout.minimumWidth: 0
            ComboBox { id: route; Layout.fillWidth: true; Layout.minimumWidth: 0; model: fields.addresses; textRole: "label"; currentIndex: Math.max(0, fields.addresses.findIndex(item => item.address === fields.destination.localAddress)); onActivated: fields.save() }
            TextField { id: code; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 190; placeholderText: "Access code (optional)"; echoMode: TextInput.Password; maximumLength: 6 }
        }
    }
    ColumnLayout {
        width: panel.availableWidth
        spacing: 12
        CheckBox { text: "Single server"; checked: panel.runtime.connections.singleServer; onClicked: panel.runtime.connections.singleServer = checked }
        Label { text: "Add resources, then check them to connect."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#9eacc2" }
        DestinationFields {
            id: shared
            visible: panel.runtime.connections.singleServer
            Layout.fillWidth: true; Layout.minimumWidth: 0
            destination: panel.runtime.connections.sharedTarget
            addresses: panel.addresses
            commit: value => panel.runtime.connections.setSharedTarget(value)
        }
        RowLayout {
            Button { text: "Disconnect all"; enabled: panel.runtime.active; onClicked: panel.runtime.disconnectAll() }
        }
        Repeater {
            model: [
                {kind: "display", title: "Displays", singular: "display"},
                {kind: "camera", title: "Cameras", singular: "camera"},
                {kind: "microphone", title: "Microphones", singular: "microphone"},
                {kind: "usb", title: "USBs", singular: "USB"}
            ]
            ColumnLayout {
                id: resourceGroup
                required property var modelData
                property bool expanded: true
                Layout.fillWidth: true; Layout.minimumWidth: 0
                spacing: 8
                function disconnectGroup() {
                    for (let i = 0; i < groupRows.count; ++i) {
                        const instance = groupRows.itemAt(i)
                        if (instance && instance.resourceKind === modelData.kind && (instance.connectionEnabled || instance.busy))
                            panel.runtime.toggle(instance.connectionId, false)
                    }
                }
                function rowCount() {
                    let count = 0
                    for (let i = 0; i < groupRows.count; ++i) {
                        const instance = groupRows.itemAt(i)
                        if (instance && instance.resourceKind === modelData.kind) ++count
                    }
                    return count
                }
                function hasConnections() {
                    for (let i = 0; i < groupRows.count; ++i) {
                        const instance = groupRows.itemAt(i)
                        if (instance && instance.resourceKind === modelData.kind && (instance.connectionEnabled || instance.busy)) return true
                    }
                    return false
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 48
                    color: "#142238"; radius: 12
                    border.color: "#243650"
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: resourceGroup.expanded = !resourceGroup.expanded
                    }
                    IconButton {
                        objectName: "connectionGroupToggle_" + resourceGroup.modelData.kind
                        anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        symbol: "chevron"; iconRotation: resourceGroup.expanded ? 90 : 0
                        description: (resourceGroup.expanded ? "Collapse " : "Expand ") + resourceGroup.modelData.title.toLowerCase()
                        onClicked: resourceGroup.expanded = !resourceGroup.expanded
                    }
                    Label {
                        objectName: "connectionGroupTitle_" + resourceGroup.modelData.kind
                        anchors.centerIn: parent
                        width: Math.max(0, parent.width - 176)
                        text: resourceGroup.modelData.title
                        font.pixelSize: 15; font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        color: "#e4edfc"
                    }
                    Row {
                        anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        IconButton {
                            objectName: "connectionGroupAdd_" + resourceGroup.modelData.kind
                            symbol: "add"; description: "Add " + resourceGroup.modelData.singular
                            onClicked: { resourceGroup.expanded = true; panel.runtime.connections.addConnection(resourceGroup.modelData.kind) }
                        }
                        IconButton {
                            objectName: "connectionGroupDisconnect_" + resourceGroup.modelData.kind
                            symbol: "disconnect"; description: "Disconnect all " + resourceGroup.modelData.title.toLowerCase()
                            enabled: resourceGroup.hasConnections()
                            onClicked: resourceGroup.disconnectGroup()
                        }
                    }
                }
                Label {
                    visible: resourceGroup.expanded && resourceGroup.rowCount() === 0
                    text: "No " + (resourceGroup.modelData.kind === "usb" ? "USBs" : resourceGroup.modelData.title.toLowerCase()) + " added."
                    color: "#7f93b0"; font.pixelSize: 12
                    Layout.leftMargin: 16; Layout.bottomMargin: 2
                }
                Repeater {
                    id: groupRows
                    model: panel.runtime.connections
                    Loader {
                        id: instance
                        required property string connectionId
                        required property string resourceKind
                        required property bool connectionEnabled
                        required property string sourceId
                        required property var target
                        required property var effectiveTarget
                        required property var displayMode
                        required property string phase
                        required property string status
                        required property bool busy
                        required property bool destinationVisible
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        // Collapse only hides the controls. The matching row and
                        // its native connection stay alive, with edited values intact.
                        active: resourceKind === resourceGroup.modelData.kind
                        visible: active && resourceGroup.expanded
                        sourceComponent: Component {
                            Frame {
                                id: row
                                padding: 16
                                background: Rectangle { color: "#101d30"; radius: 12; border.color: "#223650" }
                                readonly property string connectionId: instance.connectionId
                                readonly property string resourceKind: instance.resourceKind
                                readonly property bool connectionEnabled: instance.connectionEnabled
                                readonly property string sourceId: instance.sourceId
                                readonly property var target: instance.target
                                readonly property var effectiveTarget: instance.effectiveTarget
                                readonly property var displayMode: instance.displayMode
                                readonly property string phase: instance.phase
                                readonly property string status: instance.status
                                readonly property bool busy: instance.busy
                                readonly property bool destinationVisible: instance.destinationVisible
                                property bool displayOptionsValid: true
                                width: instance.width
                                ColumnLayout {
                                    width: parent.width; spacing: 8
                                    RowLayout {
                                        Layout.fillWidth: true; Layout.minimumWidth: 0
                                        CheckBox {
                                            text: row.resourceKind === "usb" ? "USB storage" : row.resourceKind.charAt(0).toUpperCase() + row.resourceKind.slice(1)
                                            enabled: row.connectionEnabled || row.displayOptionsValid
                                            checked: row.connectionEnabled
                                            onClicked: {
                                                if (checked && !(panel.runtime.connections.singleServer ? shared.save() : perRow.save())) { checked = Qt.binding(() => row.connectionEnabled); return }
                                                const code = panel.runtime.connections.singleServer ? shared.accessCode : perRow.accessCode
                                                panel.runtime.toggle(row.connectionId, checked, code, panel.inputAllowed)
                                                if (panel.runtime.connections.singleServer) shared.accessCode = ""; else perRow.accessCode = ""
                                            }
                                        }
                                        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                                        Button { text: "Remove"; enabled: !row.busy; onClicked: panel.runtime.connections.removeConnection(row.connectionId) }
                                    }
                                    DestinationFields { id: perRow; visible: row.destinationVisible; enabled: !row.busy; Layout.fillWidth: true; Layout.minimumWidth: 0; destination: row.target; addresses: panel.addresses; commit: value => panel.runtime.connections.setTarget(row.connectionId, value) }
                                    Label { visible: row.busy; text: "Server: " + row.effectiveTarget.address + ":" + row.effectiveTarget.port; color: "#9eacc2" }
                                    ComboBox {
                                        id: source
                                        visible: row.resourceKind !== "display"; enabled: !row.busy; Layout.fillWidth: true; Layout.minimumWidth: 0
                                        model: panel.runtime.devices[row.resourceKind] || []
                                        textRole: "label"; valueRole: "id"
                                        currentIndex: model.findIndex(item => item.id === row.sourceId)
                                        displayText: currentIndex < 0 ? "Choose " + row.resourceKind + " source" : currentText
                                        delegate: ItemDelegate { required property int index; width: source.width; text: source.model[index].label; enabled: source.model[index].available !== false; highlighted: source.highlightedIndex === index }
                                        onActivated: panel.runtime.connections.setSource(row.connectionId, currentValue)
                                        Connections { target: source.popup; function onOpened() { panel.runtime.refreshDevices(row.resourceKind) } }
                                    }
                                    ColumnLayout {
                                        visible: row.resourceKind === "display"; enabled: !row.busy; Layout.fillWidth: true; Layout.minimumWidth: 0
                                        function saveDisplay() { row.displayOptionsValid = panel.runtime.connections.setDisplayMode(row.connectionId, {mode: displayChoice.currentIndex === 0 ? "custom" : "native", monitorId: displayChoice.currentIndex === 1 ? (monitor.currentValue || "") : "", width: widthSpin.value, height: heightSpin.value, fps: fpsSpin.value}) }
                                        ComboBox { id: displayChoice; objectName: "displayModeChoice_" + row.connectionId; Layout.fillWidth: true; Layout.minimumWidth: 0; model: ["Custom resolution and frame rate", "Match local monitor"]; currentIndex: row.displayMode.mode === "native" ? 1 : 0; onActivated: parent.saveDisplay() }
                                        ComboBox { id: monitor; objectName: "displayMonitor_" + row.connectionId; visible: displayChoice.currentIndex === 1; Layout.fillWidth: true; Layout.minimumWidth: 0; model: panel.runtime.monitors; textRole: "label"; valueRole: "id"; currentIndex: model.findIndex(item => item.id === row.displayMode.monitorId); displayText: currentIndex < 0 ? "Choose a local monitor" : currentText; onActivated: parent.saveDisplay(); Connections { target: monitor.popup; function onOpened() { panel.runtime.refreshMonitors() } } }
                                        GridLayout {
                                            visible: displayChoice.currentIndex === 0
                                            Layout.fillWidth: true; Layout.minimumWidth: 0
                                            columns: width < 530 ? 1 : 3
                                            ColumnLayout {
                                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                                Label { text: "Width"; color: "#9eacc2"; font.pixelSize: 12 }
                                                SpinBox { id: widthSpin; Layout.fillWidth: true; Layout.minimumWidth: 0; from: 2; to: 4096; stepSize: 2; value: row.displayMode.width; editable: true; onValueModified: displayChoice.parent.saveDisplay() }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                                Label { text: "Height"; color: "#9eacc2"; font.pixelSize: 12 }
                                                SpinBox { id: heightSpin; Layout.fillWidth: true; Layout.minimumWidth: 0; from: 2; to: 4096; stepSize: 2; value: row.displayMode.height; editable: true; onValueModified: displayChoice.parent.saveDisplay() }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                                Label { text: "Target FPS"; color: "#9eacc2"; font.pixelSize: 12 }
                                                SpinBox { id: fpsSpin; Layout.fillWidth: true; Layout.minimumWidth: 0; from: 1; to: 360; value: Math.min(360, row.displayMode.fps); editable: true; onValueModified: displayChoice.parent.saveDisplay() }
                                            }
                                        }
                                        Label { visible: !row.displayOptionsValid; text: "This custom mode is outside the current engine limits. Use even dimensions and stay below the total-pixel limit."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#f0c66e" }
                                        Label { text: "Each display opens its own window. Limits: 4096 pixels per dimension, 8,847,360 pixels total, and 360 target FPS. Hardware and network limits apply."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#9eacc2" }
                                    }
                                    CheckBox { visible: row.resourceKind === "camera" && row.busy; text: "Mirror camera (all connections using this source)"; checked: panel.runtime.mirrorStates[row.sourceId] === true; onClicked: panel.runtime.setMirror(row.connectionId, checked) }
                                    CheckBox { visible: row.resourceKind === "microphone" && row.busy; text: "Mute microphone for this connection"; onClicked: panel.runtime.setMuted(row.connectionId, checked) }
                                    Button { visible: row.resourceKind === "usb" && row.phase === "recovery"; text: "Recheck USB availability"; onClicked: panel.runtime.refreshDevices("usb") }
                                    Label { visible: row.resourceKind === "usb"; text: "USB storage is exclusive. Uncheck to return it safely; an interrupted transfer can require recovery."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#f0c66e" }
                                    Label { visible: text.length > 0 && text !== "Disconnected"; text: row.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                                }
                            }
                        }
                    }
                }
            }
        }
        CheckBox { id: recoveryOpen; visible: panel.controller !== null; text: "Advanced: interrupted USB transfer recovery" }
        ColumnLayout {
            visible: recoveryOpen.checked && panel.controller !== null; Layout.fillWidth: true; Layout.minimumWidth: 0
            Label { text: panel.controller ? panel.controller.recoveryStatus : ""; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0 }
            ComboBox { id: recovery; Layout.fillWidth: true; Layout.minimumWidth: 0; model: panel.controller ? panel.controller.usbRecoveries : []; textRole: "label"; valueRole: "id" }
            RowLayout {
                enabled: panel.controller !== null && !panel.controller.active && !panel.controller.hosting && !panel.controller.recoveryBusy
                Button { text: "Check recovery records"; onClicked: panel.controller.refreshUsbRecovery() }
                Button { text: "Restore selected disk"; enabled: recovery.currentIndex >= 0 && recovery.model[recovery.currentIndex].available; onClicked: panel.controller.recoverUsb(recovery.currentValue) }
            }
        }
        ToolButton { id: limitDetails; text: "Connection limits"; checkable: true; font.pixelSize: 12; Accessible.name: "Show connection limits" }
        Label { visible: limitDetails.checked; text: "Current receiving limits: four displays, four cameras, four microphones and one USB storage connection per Windows server. One camera source can feed eight recipients; one microphone source can feed sixteen. Hardware and driver limits also apply."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#9eacc2" }
        Label { text: panel.runtime.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0 }
    }
}
