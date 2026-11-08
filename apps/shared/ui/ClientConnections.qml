pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ScrollView {
    id: panel
    required property var runtime
    property var controller: null
    required property var addresses
    property bool inputAllowed: true
    objectName: "clientConnectionsPanel"
    property var invalidControls: []
    property bool editShared: false
    function spinValue(control) {
        try { return Number.fromLocaleString(control.locale, control.contentItem.text) } catch (error) { return NaN }
    }
    function mark(control) {
        if (invalidControls.indexOf(control) < 0) invalidControls = invalidControls.concat([control])
    }
    function invalid(control) { return invalidControls.indexOf(control) >= 0 }
    function notifyInvalid() { connectionToastTimer.stop(); connectionToast.close(); connectedMessages = []; validationToast.open(); validationTimer.restart() }
    property var connectedMessages: []
    function notifyConnected(kind, target) {
        const name = kind === "usb" ? "USB storage" : kind.charAt(0).toUpperCase() + kind.slice(1)
        connectedMessages = connectedMessages.concat([name + " connected to " + target.address + ":" + target.port]).slice(-5)
        if (!validationToast.visible) { connectionToast.open(); connectionToastTimer.restart() }
    }
    Timer { id: connectionToastTimer; interval: 5000; onTriggered: { connectionToast.close(); panel.connectedMessages = [] } }
    Popup {
        id: connectionToast; objectName: "connectionSuccessToast"
        parent: Overlay.overlay; x: (parent.width - width) / 2; y: parent.height - height - 32
        width: Math.min(460, parent.width - 32); padding: 16; modal: false; focus: false
        background: Rectangle { color: "#193b32"; radius: 10; border.color: "#67c6a0" }
        contentItem: Label { objectName: "connectionSuccessMessage"; text: panel.connectedMessages.join("\n"); color: "white"; wrapMode: Text.WordWrap; Accessible.role: Accessible.AlertMessage }
    }
    function validAddress(value, local) {
        if (!/^\d{1,3}(\.\d{1,3}){3}$/.test(value)) return false
        const parts = value.split(".").map(Number)
        if (parts.some(v => v > 255)) return false
        return local && value === "0.0.0.0" || (parts[0] !== 127 && parts[0] < 224 && value !== "0.0.0.0")
    }
    component ErrorBorder: Rectangle {
        required property var control
        anchors.fill: parent; z: 20; color: "transparent"; radius: 5
        border.width: 2; border.color: "#ff6577"
        visible: panel.invalid(control)
    }
    Timer { id: validationTimer; interval: 5000; onTriggered: { panel.invalidControls = []; validationToast.close() } }
    Popup {
        id: validationToast; objectName: "connectionValidationToast"
        onClosed: { if (panel.connectedMessages.length) { connectionToast.open(); connectionToastTimer.restart() } }
        parent: Overlay.overlay; x: (parent.width - width) / 2; y: parent.height - height - 32
        width: Math.min(420, parent.width - 32); padding: 16; modal: false; focus: false
        background: Rectangle { color: "#48232d"; radius: 10; border.color: "#ff6577" }
        contentItem: Label { text: "Check the highlighted fields to connect."; color: "white"; wrapMode: Text.WordWrap; Accessible.role: Accessible.AlertMessage }
    }
    // Keep a stable gutter even while the scrollbar is faded out.
    rightPadding: ScrollBar.vertical.implicitWidth + 12
    contentWidth: availableWidth
    function activeTargets() {
        let targets = []
        for (let i = 0; i < resourceGroups.count; ++i) {
            const group = resourceGroups.itemAt(i)
            if (group) targets = targets.concat(group.activeTargets())
        }
        return [...new Set(targets)].join(", ")
    }
    component DestinationFields: ColumnLayout {
        id: fields
        required property var destination
        required property var addresses
        property alias accessCode: code.text
        property var commit
        property bool valid: true
        function validate() {
            let good = true
            if (!panel.validAddress(host.text.trim(), false)) { panel.mark(host); good = false }
            if (!/^\d+$/.test(port.text) || Number(port.text) < 1 || Number(port.text) > 65535) { panel.mark(port); good = false }
            if (route.currentIndex < 0 || route.currentIndex >= addresses.length || !panel.validAddress(addresses[route.currentIndex].address, true)) { panel.mark(route); good = false }
            if (code.text.length && !/^\d{6}$/.test(code.text)) { panel.mark(code); good = false }
            return good
        }
        function rejectCode() { panel.mark(code); panel.editShared = true; panel.notifyInvalid() }
        function save() {
            const local = addresses[route.currentIndex]
            valid = commit({address: host.text.trim(), port: Number(port.text), localAddress: local ? local.address : "0.0.0.0", interfaceIndex: local ? local.index : 0})
            return valid
        }
        Label { visible: !fields.valid; text: "Enter a valid IPv4 address and port."; color: "#f0c66e" }
        RowLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            TextField { id: host; objectName: "connectionAddress"; ErrorBorder { control: host } Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: "Server IPv4 address"; text: fields.destination.address; onEditingFinished: fields.save() }
            TextField { id: port; ErrorBorder { control: port } Layout.preferredWidth: 100; placeholderText: "Port"; text: fields.destination.port; validator: IntValidator { bottom: 1; top: 65535 } onEditingFinished: fields.save() }
        }
        GridLayout {
            columns: fields.width < 480 ? 1 : 2
            Layout.fillWidth: true; Layout.minimumWidth: 0
            ComboBox { id: route; ErrorBorder { control: route } Layout.fillWidth: true; Layout.minimumWidth: 0; model: fields.addresses; textRole: "label"; currentIndex: Math.max(0, fields.addresses.findIndex(item => item.address === fields.destination.localAddress)); onActivated: fields.save() }
            TextField { id: code; objectName: "connectionCode"; ErrorBorder { control: code } Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 190; placeholderText: "Access code (optional)"; echoMode: TextInput.Password; maximumLength: 6 }
        }
    }
    ColumnLayout {
        width: panel.availableWidth
        spacing: 12
        CheckBox { text: "Single server"; checked: panel.runtime.connections.singleServer; onClicked: panel.runtime.connections.singleServer = checked }
        Label { text: "Add resources, then use the green power button to connect."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#9eacc2" }
        DestinationFields {
            id: shared
            objectName: "sharedDestinationFields"
            visible: panel.runtime.connections.singleServer && (panel.runtime.connections.activeConnectionCount === 0 || panel.editShared)
            Layout.fillWidth: true; Layout.minimumWidth: 0
            destination: panel.runtime.connections.sharedTarget
            addresses: panel.addresses
            commit: value => panel.runtime.connections.setSharedTarget(value)
        }
        Label {
            objectName: "sharedDestinationSummary"
            visible: panel.runtime.connections.singleServer && !shared.visible
            text: "Connected to " + panel.activeTargets()
            color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        RowLayout {
            visible: panel.runtime.active
            Button { text: "Disconnect all"; background: Rectangle { implicitHeight: 44; radius: 10; color: !parent.enabled ? "#27364c" : parent.down ? "#8f1d2a" : parent.hovered ? "#ce3949" : "#b32635"; border.width: parent.activeFocus ? 2 : 0; border.color: "#ff9da8" } Material.foreground: "white"; enabled: panel.runtime.active; onClicked: panel.runtime.disconnectAll() }
        }
        Repeater {
            id: resourceGroups
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
                function activeTargets() {
                    const targets = []
                    for (let i = 0; i < groupRows.count; ++i) {
                        const instance = groupRows.itemAt(i)
                        if (instance && instance.resourceKind === modelData.kind && instance.busy)
                            targets.push(instance.effectiveTarget.address + ":" + instance.effectiveTarget.port)
                    }
                    return targets
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
                            symbol: "disconnect"; destructive: true; description: "Disconnect all " + resourceGroup.modelData.title.toLowerCase()
                            visible: enabled
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
                                function validateConnection() {
                                    panel.invalidControls = []
                                    const fields = panel.runtime.connections.singleServer ? shared : perRow
                                    let good = fields.validate()
                                    if (resourceKind !== "display") {
                                        if (source.currentIndex < 0 || source.model[source.currentIndex].available === false) { panel.mark(source); good = false }
                                    } else if (displayChoice.currentIndex === 1) {
                                        if (monitor.currentIndex < 0) { panel.mark(monitor); good = false }
                                    } else {
                                        const w = panel.spinValue(widthSpin), h = panel.spinValue(heightSpin), f = panel.spinValue(fpsSpin)
                                        if (!Number.isInteger(w) || w < 2 || w > 4096 || w % 2) { panel.mark(widthSpin); good = false }
                                        if (!Number.isInteger(h) || h < 2 || h > 4096 || h % 2) { panel.mark(heightSpin); good = false }
                                        if (w * h > 8847360) { panel.mark(widthSpin); panel.mark(heightSpin); good = false }
                                        if (!Number.isInteger(f) || f < 1 || f > 360) { panel.mark(fpsSpin); good = false }
                                        if (good) { widthSpin.value = w; heightSpin.value = h; fpsSpin.value = f }
                                    }
                                    if (!good) { panel.notifyInvalid(); return false }
                                    if (resourceKind === "display") displayChoice.parent.saveDisplay()
                                    if (!fields.save()) { panel.mark(fields); panel.notifyInvalid(); return false }
                                    validationToast.close(); return true
                                }
                                onStatusChanged: {
                                    if (phase === "failed" && /access.code|authentication|unauthorized/i.test(status))
                                        (panel.runtime.connections.singleServer ? shared : perRow).rejectCode()
                                }
                                property bool displayOptionsValid: true
                                property string connectedDescription: ""
                                property bool microphoneMuted: false
                                function freezeDescription() {
                                    if (!busy) return
                                    if (resourceKind === "display") {
                                        const mode = displayMode
                                        const local = (panel.runtime.monitors || []).find(item => item.id === mode.monitorId)
                                        connectedDescription = mode.mode === "native"
                                            ? (local ? local.width + " x " + local.height + " @ " + Math.min(60, local.fps) + " target FPS (local monitor)" : "Local monitor native mode")
                                            : mode.width + " x " + mode.height + " @ " + mode.fps + " target FPS"
                                    } else {
                                        const device = (panel.runtime.devices[resourceKind] || []).find(item => item.id === sourceId)
                                        connectedDescription = device ? device.label : sourceId || resourceKind
                                    }
                                }
                                onPhaseChanged: { if (phase === "active") panel.notifyConnected(resourceKind, effectiveTarget) }
                                onBusyChanged: { freezeDescription(); if (!busy) microphoneMuted = false }
                                Component.onCompleted: freezeDescription()
                                width: instance.width
                                ColumnLayout {
                                    width: parent.width; spacing: 8
                                    RowLayout {
                                        visible: !row.busy
                                        Layout.fillWidth: true; Layout.minimumWidth: 0
                                        Label {
                                            text: row.resourceKind === "usb" ? "USB storage" : row.resourceKind.charAt(0).toUpperCase() + row.resourceKind.slice(1)
                                        }
                                        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                                        IconButton {
                                            objectName: "connectionEnable_" + row.connectionId
                                            symbol: "disconnect"; positive: true; description: "Connect " + row.resourceKind
                                            implicitWidth: 44; implicitHeight: 44
                                            onClicked: {
                                                if (!row.validateConnection()) return
                                                const code = panel.runtime.connections.singleServer ? shared.accessCode : perRow.accessCode
                                                panel.runtime.toggle(row.connectionId, true, code, panel.inputAllowed)
                                                panel.editShared = false
                                                if (panel.runtime.connections.singleServer) shared.accessCode = ""; else perRow.accessCode = ""
                                            }
                                        }
                                        Button { text: "Remove"; enabled: !row.busy; onClicked: panel.runtime.connections.removeConnection(row.connectionId) }
                                    }
                                    DestinationFields { id: perRow; visible: row.destinationVisible && !row.busy; enabled: !row.busy; Layout.fillWidth: true; Layout.minimumWidth: 0; destination: row.target; addresses: panel.addresses; commit: value => panel.runtime.connections.setTarget(row.connectionId, value) }
                                    RowLayout {
                                        visible: row.busy; Layout.fillWidth: true; Layout.minimumWidth: 0
                                        ColumnLayout {
                                            Layout.fillWidth: true; Layout.minimumWidth: 0
                                            Label { objectName: "connectedTarget_" + row.connectionId; text: row.effectiveTarget.address + ":" + row.effectiveTarget.port; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                            Label { objectName: "connectedDescription_" + row.connectionId; text: row.connectedDescription; color: "#9eacc2"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                                        }
                                        IconButton {
                                            objectName: "connectionDisconnect_" + row.connectionId
                                            symbol: "disconnect"; destructive: true; description: "Disconnect " + row.resourceKind
                                            implicitWidth: 44; implicitHeight: 44
                                            enabled: row.phase !== "returning" && row.phase !== "recovery"
                                            onClicked: panel.runtime.toggle(row.connectionId, false)
                                        }
                                    }
                                    ComboBox {
                                        id: source
                                        visible: row.resourceKind !== "display" && !row.busy; enabled: !row.busy; Layout.fillWidth: true; Layout.minimumWidth: 0
                                        model: panel.runtime.devices[row.resourceKind] || []
                                        textRole: "label"; valueRole: "id"
                                        currentIndex: model.findIndex(item => item.id === row.sourceId)
                                        displayText: currentIndex < 0 ? "Choose " + row.resourceKind + " source" : currentText
                                        delegate: ItemDelegate { required property int index; width: source.width; text: source.model[index].label; enabled: source.model[index].available !== false; highlighted: source.highlightedIndex === index }
                                        onActivated: panel.runtime.connections.setSource(row.connectionId, currentValue)
                                        Connections { target: source.popup; function onOpened() { panel.runtime.refreshDevices(row.resourceKind) } }
                                    }
                                    ColumnLayout {
                                        visible: row.resourceKind === "display" && !row.busy; enabled: !row.busy; Layout.fillWidth: true; Layout.minimumWidth: 0
                                        function saveDisplay() { row.displayOptionsValid = panel.runtime.connections.setDisplayMode(row.connectionId, {mode: displayChoice.currentIndex === 0 ? "custom" : "native", monitorId: displayChoice.currentIndex === 1 ? (monitor.currentValue || "") : "", width: widthSpin.value, height: heightSpin.value, fps: fpsSpin.value}) }
                                        ComboBox { id: displayChoice; objectName: "displayModeChoice_" + row.connectionId; Layout.fillWidth: true; Layout.minimumWidth: 0; model: ["Custom resolution and frame rate", "Match local monitor"]; currentIndex: row.displayMode.mode === "native" ? 1 : 0; onActivated: parent.saveDisplay() }
                                        ComboBox { id: monitor; ErrorBorder { control: monitor } objectName: "displayMonitor_" + row.connectionId; visible: displayChoice.currentIndex === 1; Layout.fillWidth: true; Layout.minimumWidth: 0; model: panel.runtime.monitors; textRole: "label"; valueRole: "id"; currentIndex: model.findIndex(item => item.id === row.displayMode.monitorId); displayText: currentIndex < 0 ? "Choose a local monitor" : currentText; onActivated: parent.saveDisplay(); Connections { target: monitor.popup; function onOpened() { panel.runtime.refreshMonitors() } } }
                                        GridLayout {
                                            visible: displayChoice.currentIndex === 0
                                            Layout.fillWidth: true; Layout.minimumWidth: 0
                                            columns: width < 530 ? 1 : 3
                                            ColumnLayout {
                                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                                Label { text: "Width"; color: "#9eacc2"; font.pixelSize: 12 }
                                                SpinBox { id: widthSpin; ErrorBorder { control: widthSpin } Layout.fillWidth: true; Layout.minimumWidth: 0; from: 2; to: 4096; stepSize: 2; value: row.displayMode.width; editable: true; onValueModified: displayChoice.parent.saveDisplay() }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                                Label { text: "Height"; color: "#9eacc2"; font.pixelSize: 12 }
                                                SpinBox { id: heightSpin; ErrorBorder { control: heightSpin } Layout.fillWidth: true; Layout.minimumWidth: 0; from: 2; to: 4096; stepSize: 2; value: row.displayMode.height; editable: true; onValueModified: displayChoice.parent.saveDisplay() }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                                Label { text: "Target FPS"; color: "#9eacc2"; font.pixelSize: 12 }
                                                SpinBox { id: fpsSpin; ErrorBorder { control: fpsSpin } Layout.fillWidth: true; Layout.minimumWidth: 0; from: 1; to: 360; value: Math.min(360, row.displayMode.fps); editable: true; onValueModified: displayChoice.parent.saveDisplay() }
                                            }
                                        }
                                        Label { visible: !row.displayOptionsValid; text: "This custom mode is outside the current engine limits. Use even dimensions and stay below the total-pixel limit."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#f0c66e" }
                                        Label { text: "Each display opens its own window. Limits: 4096 pixels per dimension, 8,847,360 pixels total, and 360 target FPS. Hardware and network limits apply."; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0; color: "#9eacc2" }
                                    }
                                    CheckBox { visible: row.resourceKind === "camera" && row.busy; text: "Mirror camera (all connections using this source)"; checked: panel.runtime.mirrorStates[row.sourceId] === true; onClicked: panel.runtime.setMirror(row.connectionId, checked) }
                                    CheckBox { visible: row.resourceKind === "microphone" && row.busy; text: "Mute microphone for this connection"; checked: row.microphoneMuted; onClicked: { row.microphoneMuted = checked; panel.runtime.setMuted(row.connectionId, checked) } }
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
        CheckBox { id: recoveryOpen; visible: panel.controller !== null && typeof panel.controller.refreshUsbRecovery === "function"; text: "Advanced: interrupted USB transfer recovery" }
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
        Label { text: panel.runtime.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0 }
    }
}
