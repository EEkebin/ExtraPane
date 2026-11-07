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
            Layout.fillWidth: true
            TextField { id: host; Layout.fillWidth: true; placeholderText: "Server IPv4 address"; text: fields.destination.address; onEditingFinished: fields.save() }
            TextField { id: port; Layout.preferredWidth: 100; placeholderText: "Port"; text: fields.destination.port; validator: IntValidator { bottom: 1; top: 65535 } onEditingFinished: fields.save() }
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox { id: route; Layout.fillWidth: true; model: fields.addresses; textRole: "label"; currentIndex: Math.max(0, fields.addresses.findIndex(item => item.address === fields.destination.localAddress)); onActivated: fields.save() }
            TextField { id: code; Layout.preferredWidth: 190; placeholderText: "Access code (optional)"; echoMode: TextInput.Password; maximumLength: 6 }
        }
    }
    ColumnLayout {
        width: panel.availableWidth
        spacing: 12
        CheckBox { text: "Single server"; checked: panel.runtime.connections.singleServer; onClicked: panel.runtime.connections.singleServer = checked }
        Label { text: "Choose resources and enable their checkboxes to connect. Active connections keep their destination until stopped and reconnected."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#9eacc2" }
        DestinationFields {
            id: shared
            visible: panel.runtime.connections.singleServer
            Layout.fillWidth: true
            destination: panel.runtime.connections.sharedTarget
            addresses: panel.addresses
            commit: value => panel.runtime.connections.setSharedTarget(value)
        }
        RowLayout {
            Button { text: "Disconnect all"; enabled: panel.runtime.active; onClicked: panel.runtime.disconnectAll() }
        }
        Flow {
            Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 8
            Repeater {
                model: ["display", "camera", "microphone", "usb"]
                Button { required property string modelData; text: "+ " + (modelData === "usb" ? "USB" : modelData.charAt(0).toUpperCase() + modelData.slice(1)); onClicked: panel.runtime.connections.addConnection(modelData) }
            }
        }
        Repeater {
            model: panel.runtime.connections
            Frame {
                id: row
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
                property bool displayOptionsValid: true
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
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
                        Item { Layout.fillWidth: true }
                        Button { text: "Remove"; enabled: !row.busy; onClicked: panel.runtime.connections.removeConnection(row.connectionId) }
                    }
                    DestinationFields { id: perRow; visible: row.destinationVisible; enabled: !row.busy; Layout.fillWidth: true; destination: row.target; addresses: panel.addresses; commit: value => panel.runtime.connections.setTarget(row.connectionId, value) }
                    Label { visible: row.busy; text: "Server: " + row.effectiveTarget.address + ":" + row.effectiveTarget.port; color: "#9eacc2" }
                    ComboBox {
                        id: source
                        visible: row.resourceKind !== "display"; enabled: !row.busy; Layout.fillWidth: true
                        model: panel.runtime.devices[row.resourceKind] || []
                        textRole: "label"; valueRole: "id"
                        currentIndex: model.findIndex(item => item.id === row.sourceId)
                        displayText: currentIndex < 0 ? "Choose " + row.resourceKind + " source" : currentText
                        delegate: ItemDelegate { required property int index; width: source.width; text: source.model[index].label; enabled: source.model[index].available !== false; highlighted: source.highlightedIndex === index }
                        onActivated: panel.runtime.connections.setSource(row.connectionId, currentValue)
                        Connections { target: source.popup; function onOpened() { panel.runtime.refreshDevices(row.resourceKind) } }
                    }
                    ColumnLayout {
                        visible: row.resourceKind === "display"; enabled: !row.busy; Layout.fillWidth: true
                        function saveDisplay() { row.displayOptionsValid = panel.runtime.connections.setDisplayMode(row.connectionId, {mode: displayChoice.currentIndex === 0 ? "custom" : "native", monitorId: displayChoice.currentIndex === 1 ? (monitor.currentValue || "") : "", width: widthSpin.value, height: heightSpin.value, fps: fpsSpin.value}) }
                        ComboBox { id: displayChoice; objectName: "displayModeChoice_" + row.connectionId; Layout.fillWidth: true; model: ["Custom resolution and frame rate", "Match local monitor"]; currentIndex: row.displayMode.mode === "native" ? 1 : 0; onActivated: parent.saveDisplay() }
                        ComboBox { id: monitor; objectName: "displayMonitor_" + row.connectionId; visible: displayChoice.currentIndex === 1; Layout.fillWidth: true; model: panel.runtime.monitors; textRole: "label"; valueRole: "id"; currentIndex: model.findIndex(item => item.id === row.displayMode.monitorId); displayText: currentIndex < 0 ? "Choose a local monitor" : currentText; onActivated: parent.saveDisplay(); Connections { target: monitor.popup; function onOpened() { panel.runtime.refreshMonitors() } } }
                        RowLayout {
                            visible: displayChoice.currentIndex === 0
                            SpinBox { id: widthSpin; from: 2; to: 4096; stepSize: 2; value: row.displayMode.width; editable: true; onValueModified: parent.parent.saveDisplay() }
                            Label { text: "×" }
                            SpinBox { id: heightSpin; from: 2; to: 4096; stepSize: 2; value: row.displayMode.height; editable: true; onValueModified: parent.parent.saveDisplay() }
                            SpinBox { id: fpsSpin; from: 1; to: 60; value: Math.min(60, row.displayMode.fps); editable: true; onValueModified: parent.parent.saveDisplay() }
                            Label { text: "FPS" }
                        }
                        Label { visible: !row.displayOptionsValid; text: "This custom mode is outside the current engine limits. Use even dimensions and stay below the total-pixel limit."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#f0c66e" }
                        Label { text: "Each display opens a resizable window. Current engine: up to 4096 × 4096 and 60 FPS, subject to host support. This does not change the physical monitor's refresh rate."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#9eacc2" }
                    }
                    CheckBox { visible: row.resourceKind === "camera" && row.busy; text: "Mirror camera (all connections using this source)"; checked: panel.runtime.mirrorStates[row.sourceId] === true; onClicked: panel.runtime.setMirror(row.connectionId, checked) }
                    CheckBox { visible: row.resourceKind === "microphone" && row.busy; text: "Mute microphone for this connection"; onClicked: panel.runtime.setMuted(row.connectionId, checked) }
                    Button { visible: row.resourceKind === "usb" && row.phase === "recovery"; text: "Recheck USB availability"; onClicked: panel.runtime.refreshDevices("usb") }
                    Label { visible: row.resourceKind === "usb"; text: "USB storage is exclusive. Uncheck to return it safely; an interrupted transfer can require recovery."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#f0c66e" }
                    Label { visible: text.length > 0 && text !== "Disconnected"; text: row.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
        CheckBox { id: recoveryOpen; visible: panel.controller !== null; text: "Advanced: interrupted USB transfer recovery" }
        ColumnLayout {
            visible: recoveryOpen.checked && panel.controller !== null; Layout.fillWidth: true
            Label { text: panel.controller ? panel.controller.recoveryStatus : ""; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ComboBox { id: recovery; Layout.fillWidth: true; model: panel.controller ? panel.controller.usbRecoveries : []; textRole: "label"; valueRole: "id" }
            RowLayout {
                enabled: panel.controller !== null && !panel.controller.active && !panel.controller.hosting && !panel.controller.recoveryBusy
                Button { text: "Check recovery records"; onClicked: panel.controller.refreshUsbRecovery() }
                Button { text: "Restore selected disk"; enabled: recovery.currentIndex >= 0 && recovery.model[recovery.currentIndex].available; onClicked: panel.controller.recoverUsb(recovery.currentValue) }
            }
        }
        Label { text: "Current receiving limits: four displays, four cameras, four microphones and one USB storage connection per Windows server. One camera source can feed eight recipients; one microphone source can feed sixteen. Hardware and driver limits also apply."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#9eacc2" }
        Label { text: panel.runtime.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
}
