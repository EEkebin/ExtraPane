import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: workspace
    property int page: 0
    property bool collapsed: false
    property string expectedDeviceId: ""
    property real expansion: collapsed ? 0 : 1
    Behavior on expansion { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Connections { target: peers; function onToast(message) { notification.text = message; notification.open() } }
    Popup {
        id: notification
        property string text: ""
        x: Math.max(12, (workspace.width-width)/2); y: workspace.height-height-24
        width: Math.min(480, workspace.width-24); padding: 16
        background: Rectangle { color: "#243a57"; radius: 12; border.color: "#58779d" }
        contentItem: Label { text: notification.text; wrapMode: Text.WordWrap }
        onOpened: toastTimer.restart()
        Timer { id: toastTimer; interval: 4500; onTriggered: notification.close() }
    }
    component Action: Button {
        property bool destructive: false
        leftPadding: 18; rightPadding: 18
        background: Rectangle { implicitHeight: 44; radius: 10; color: !parent.enabled ? "#243147" : parent.destructive ? (parent.hovered ? "#c33849" : "#a52b3b") : (parent.hovered ? "#3e7ab7" : "#2b6099") }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 72+152*workspace.expansion; color: "#0a1120"; clip: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 104
                    Image { id: logo; source: "qrc:/extrapane.png"; width: 30; height: 30; x: 9; y: 12 }
                    Label { text: "ExtraPane"; font.pixelSize: 20; anchors.left: logo.right; anchors.leftMargin: 8; anchors.verticalCenter: logo.verticalCenter; opacity: workspace.expansion }
                    IconButton { x: 6; y: 56; symbol: "chevron"; rotation: 180*workspace.expansion; transformOrigin: Item.Center; description: workspace.collapsed ? "Expand navigation" : "Collapse navigation"; onClicked: workspace.collapsed=!workspace.collapsed }
                }
                Repeater {
                    model: [{name:"Devices", icon:"client"},{name:"Share", icon:"server"},{name:"Connections", icon:"settings"}]
                    ToolButton {
                        id: nav
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 48; padding: 0
                        Accessible.name: modelData.name
                        onClicked: workspace.page=index
                        ToolTip.visible: hovered && workspace.collapsed; ToolTip.text: modelData.name
                        background: Rectangle { radius: 10; color: workspace.page===nav.index ? "#192e4b" : nav.hovered ? "#132136" : "transparent" }
                        contentItem: Item {
                            AppIcon { id: glyph; symbol: nav.modelData.icon; width: 22; height: 22; anchors.verticalCenter: parent.verticalCenter; x: 14*workspace.expansion+(parent.width-width)/2*(1-workspace.expansion) }
                            Label { text: nav.modelData.name; anchors.left: glyph.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; opacity: workspace.expansion }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                ToolButton { text: workspace.collapsed ? "?" : "About ExtraPane"; Layout.fillWidth: true; onClicked: workspace.page=3 }
            }
        }
        ScrollView {
            id: pageScroll
            objectName: "peerPageScroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            leftPadding: workspace.width<700 ? 16 : 32; topPadding: 32; bottomPadding: 32
            rightPadding: ScrollBar.vertical.implicitWidth+32; contentWidth: availableWidth
            ColumnLayout {
                objectName: "peerPageContent"
                width: pageScroll.availableWidth; spacing: 18
                Label { text: ["Devices","Share","Connections","ExtraPane"][workspace.page]; font.pixelSize: 30; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aabbd3"; text: workspace.page===0 ? "Use resources from another device. Connect over your local network or enter a reachable address." : workspace.page===1 ? "Choose what other devices can use. Sharing a resource does not activate it until someone connects." : workspace.page===2 ? "Each resource has its own connection. Stop one without interrupting the others." : "Displays, cameras, microphones, speakers and USB devices across your computers." }
                ColumnLayout {
                    visible: workspace.page===0; Layout.fillWidth: true; spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: host; objectName: "peerAddress"; onTextEdited: workspace.expectedDeviceId=""; Layout.fillWidth: true; placeholderText: "Address or localhost"; Accessible.name: "Device address" }
                        TextField { id: port; Layout.preferredWidth: 100; text: "43824"; placeholderText: "Port"; validator: IntValidator { bottom: 1; top: 65535 } }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: code; Layout.fillWidth: true; placeholderText: "Pairing code (first connection)"; maximumLength: 6; echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly }
                        Action { objectName: "peerConnect"; text: "Connect"; onClicked: { peers.connectDevice(host.text,Number(port.text),code.text,workspace.expectedDeviceId,"",openConnection.checked); code.clear() } }
                    }
                    CheckBox { id: openConnection; text: "Connect without pairing"; checked: false }
                    Label { visible: openConnection.checked; text: "The other device must also have pairing disabled. Its identity will not be verified."; color: "#f0c66e"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout {
                        Label { text: "Nearby devices"; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Button { text: peers.directory.scanning ? "Refresh" : "Find devices"; onClicked: { if (peers.directory.scanning) peers.directory.refresh(); else peers.directory.start() } }
                    }
                    Label { text: peers.directory.status; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: peers.directory.devices
                        delegate: ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.name+" - "+modelData.address+":"+modelData.port
                            onClicked: { host.text=modelData.address; port.text=String(modelData.port); workspace.expectedDeviceId=modelData.id; code.forceActiveFocus() }
                        }
                    }
                    Label { visible: peers.remembered.length>0; text: "Paired devices"; font.weight: Font.DemiBold }
                    Repeater {
                        model: peers.remembered
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: modelData.address ? modelData.address+":"+modelData.port : "Choose this device from discovery or enter its address"; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                            Action { objectName: "peerRemembered_"+modelData.id; text: "Connect"; enabled: !!modelData.address && modelData.port>0; onClicked: peers.connectDevice(modelData.address,modelData.port,"",modelData.id,"",false) }
                            Action { objectName: "peerForget_"+modelData.id; text: "Forget"; destructive: true; onClicked: peers.forgetDevice(modelData.id) }
                        }
                    }
                    Repeater {
                        model: peers.devices
                        delegate: ColumnLayout {
                            id: peerDevice
                            required property var modelData
                            visible: !modelData.incoming; Layout.fillWidth: true; spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: modelData.name+(modelData.verified ? " (verified)" : " (unverified)"); font.pixelSize: 18; Layout.fillWidth: true; elide: Text.ElideRight }
                                Action { text: "Disconnect"; destructive: true; onClicked: peers.disconnectDevice(modelData.connection) }
                            }
                            Repeater {
                                model: modelData.resources
                                delegate: Rectangle {
                                    id: resourceCard
                                    required property var modelData
                                    objectName: "peerResource_"+modelData.id
                                    property string connectionId: peerDevice.modelData.connection
                                    Layout.fillWidth: true; implicitHeight: resourceContent.implicitHeight+28; radius: 12; color: "#162339"
                                    ColumnLayout {
                                        id: resourceContent; anchors.fill: parent; anchors.margins: 14; spacing: 10
                                        Label { text: resourceCard.modelData.label; font.weight: Font.DemiBold }
                                        Label { visible: !resourceCard.modelData.available; text: resourceCard.modelData.reason; color: "#f0c66e"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                        CheckBox { id: input; visible: resourceCard.modelData.inputAllowed; checked: resourceCard.modelData.inputAllowed&&resourceCard.modelData.kindId===0; text: "Allow input" }
                                        RowLayout {
                                            visible: resourceCard.modelData.kindId===0
                                            TextField { id: modeWidth; text: String(resourceCard.modelData.width||1920); placeholderText: "Width"; Layout.fillWidth: true; validator: IntValidator { bottom:64; top:4096 } }
                                            TextField { id: modeHeight; text: String(resourceCard.modelData.height||1080); placeholderText: "Height"; Layout.fillWidth: true; validator: IntValidator { bottom:64; top:4096 } }
                                            TextField { id: modeRefresh; text: String(Math.min(resourceCard.modelData.refresh||60,360)); placeholderText: "Hz / FPS"; Layout.preferredWidth: 90; validator: IntValidator { bottom:1; top:360 } }
                                        }
                                        Action { objectName: "peerUse_"+resourceCard.modelData.id; text: resourceCard.modelData.kind; enabled: resourceCard.modelData.available; onClicked: peers.useResource(resourceCard.connectionId,resourceCard.modelData.owner,resourceCard.modelData.id,resourceCard.modelData.inputAllowed&&input.checked,resourceCard.modelData.kindId===0?Number(modeWidth.text):0,resourceCard.modelData.kindId===0?Number(modeHeight.text):0,resourceCard.modelData.kindId===0?Number(modeRefresh.text):0) }
                                    }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: workspace.page===1; Layout.fillWidth: true; spacing: 14
                    Label { text: peers.deviceName; font.pixelSize: 20 }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox { id: bind; Layout.fillWidth: true; editable: true; model: peers.networkAddresses; textRole: "label"; valueRole: "address"; enabled: !peers.sharing; property string selectedAddress: currentValue||editText; onActivated: selectedAddress=currentValue; onEditTextChanged: { if (activeFocus) selectedAddress=editText } }
                        TextField { id: listenPort; text: "43824"; Layout.preferredWidth: 100; placeholderText: "Port"; enabled: !peers.sharing; validator: IntValidator { bottom:1; top:65535 } }
                    }
                    CheckBox { text: "Require pairing"; checked: peers.pairingRequired; onToggled: peers.pairingRequired=checked }
                    Label { visible: peers.pairingRequired; text: "Pairing code: "+peers.pairingCode; font.pixelSize: 24; font.letterSpacing: 2 }
                    Label { visible: peers.pairingRequired; text: "The code stays the same until ExtraPane restarts. Previously paired devices use their saved identity."; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: peers.offers
                        delegate: CheckBox {
                            required property var modelData
                            Layout.fillWidth: true; text: modelData.label; checked: modelData.enabled; enabled: modelData.available
                            onToggled: peers.enableOffer(modelData.id,checked)
                            ToolTip.visible: hovered && !modelData.available; ToolTip.text: modelData.reason
                        }
                    }
                    CheckBox { text: "Allow paired devices to use my paired devices"; checked: peers.gatewayEnabled; onToggled: peers.gatewayEnabled=checked }
                    Label { visible: peers.gatewayEnabled; text: "ExtraPane relays permitted resources through this computer. Turning this off ends relayed connections. It does not open ports on your router."; color: "#aabbd3"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Action { text: peers.sharing ? "Stop sharing" : "Start sharing"; destructive: peers.sharing; onClicked: { if (peers.sharing) peers.stopSharing(); else peers.startSharing(bind.selectedAddress,Number(listenPort.text)) } }
                }
                ColumnLayout {
                    visible: workspace.page===2; Layout.fillWidth: true; spacing: 14
                    Label { visible: peers.connections.length===0; text: "No resources connected"; color: "#aabbd3" }
                    Action { visible: peers.connections.length>0; text: "Disconnect all"; destructive: true; onClicked: peers.disconnectAll() }
                    Repeater {
                        model: peers.connections
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: modelData.resource||"Shared resource"; elide: Text.ElideRight }
                            Action { text: "Disconnect"; destructive: true; onClicked: peers.stopResource(modelData.connection,modelData.grant) }
                        }
                    }
                }
                ColumnLayout {
                    visible: workspace.page===3; Layout.fillWidth: true; spacing: 18
                    Image { source: "qrc:/extrapane.png"; Layout.preferredWidth: 96; Layout.preferredHeight: 96; Layout.alignment: Qt.AlignRight }
                    Label { text: "Device-sharing development build"; color: "#aabbd3" }
                    Button { text: "Read the documentation"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane#readme") }
                    Button { text: "Releases and changelog"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane/releases") }
                    Button { text: "GitHub repository"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane") }
                }
                Label { Layout.fillWidth: true; text: peers.status; wrapMode: Text.WordWrap; color: "#aabbd3" }
            }
        }
    }
}
