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
        contentItem: Label { textFormat: Text.PlainText; text: notification.text; wrapMode: Text.WordWrap }
        onOpened: toastTimer.restart()
        Timer { id: toastTimer; interval: 4500; onTriggered: notification.close() }
    }
    function validAddress(value) {
        const text=value.trim(); if (text.toLowerCase()==="localhost") return true
        const parts=text.split("."); if(parts.length!==4) return false
        for(let i=0;i<4;i++) if(!/^\d{1,3}$/.test(parts[i])||Number(parts[i])>255) return false
        return Number(parts[0])>0&&Number(parts[0])<224
    }
    function checkFields(fields) {
        let bad=false
        for(const field of fields) { field.invalid=!field.acceptableInput||field.text.trim()===""; bad=bad||field.invalid }
        if(bad) { notification.text="Check the highlighted information."; notification.open() }
        return !bad
    }
    component Field: TextField {
        id: field
        property bool invalid: false
        readonly property bool nativeOutline: background !== null && typeof background.outlineColor !== "undefined"
        onInvalidChanged: if(invalid) invalidTimer.restart()
        onTextEdited: invalid=false
        // Recolor the native outline so its floating-label notch stays intact.
        Binding { target: field.nativeOutline ? field.background : null; property: "outlineColor"; value: "#ff6978"; when: field.nativeOutline && field.invalid; restoreMode: Binding.RestoreBindingOrValue }
        Binding { target: field.nativeOutline ? field.background : null; property: "focusedOutlineColor"; value: "#ff6978"; when: field.nativeOutline && field.invalid; restoreMode: Binding.RestoreBindingOrValue }
        Rectangle { anchors.fill: parent; color: "transparent"; radius: 5; border.width: 2; border.color: "#ff6978"; visible: field.invalid && !field.nativeOutline }
        Timer { id: invalidTimer; interval: 4500; onTriggered: field.invalid=false }
    }
    component Action: Button {
        property bool destructive: false
        leftPadding: 18; rightPadding: 18
        background: Rectangle { implicitHeight: 44; radius: 10; color: !parent.enabled ? "#243147" : parent.destructive ? (parent.hovered ? "#c33849" : "#a52b3b") : (parent.hovered ? "#3e7ab7" : "#2b6099") }
    }
    component PowerButton: IconButton {
        symbol: "disconnect"
        implicitWidth: 48; implicitHeight: 48; padding: 12
        positive: !destructive
    }
    readonly property var resourceGroups: [
        {key:"monitors", title:"Monitors", icon:"display", kinds:[0,5]},
        {key:"cameras", title:"Cameras", icon:"camera", kinds:[1]},
        {key:"speakers", title:"Speakers", icon:"speaker", kinds:[4]},
        {key:"microphones", title:"Microphones", icon:"microphone", kinds:[2]},
        {key:"usb", title:"USB", icon:"usb", kinds:[3]}
    ]
    function groupedRows(rows, group) {
        return (rows || []).filter(row => group.kinds.indexOf(row.kindId) >= 0)
    }
    component ResourceGroup: ColumnLayout {
        id: group
        property string title: ""
        property string symbol: ""
        property int count: 0
        property bool expanded: true
        property string groupKey: ""
        default property alias rows: groupBody.data
        Layout.fillWidth: true; spacing: expanded ? 10 : 0
        data: [ToolButton {
            id: groupHeader
            objectName: group.groupKey+"Header"
            Layout.fillWidth: true; Layout.preferredHeight: 54; padding: 14
            Accessible.name: group.title+", "+group.count+" resources, "+(group.expanded ? "expanded" : "collapsed")
            onClicked: group.expanded=!group.expanded
            background: Rectangle { radius: 10; color: groupHeader.hovered ? "#20354f" : "#192b43" }
            contentItem: Item {
                AppIcon { symbol: group.symbol; width: 22; height: 22; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                Label { text: group.title+" ("+group.count+")"; font.weight: Font.DemiBold; anchors.centerIn: parent; width: Math.max(0,parent.width-68); horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                AppIcon { symbol: "chevron"; width: 18; height: 18; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; rotation: group.expanded ? 90 : 0; Behavior on rotation { NumberAnimation { duration: 160 } } }
            }
        }, ColumnLayout {
            id: groupBody
            objectName: group.groupKey+"Body"
            visible: group.expanded; Layout.fillWidth: true; spacing: 10
        }]
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
                    Label { textFormat: Text.PlainText; text: "ExtraPane"; font.pixelSize: 20; anchors.left: logo.right; anchors.leftMargin: 8; anchors.verticalCenter: logo.verticalCenter; opacity: workspace.expansion }
                    IconButton { objectName: "peerNavigationMenu"; x: 6; y: 56; symbol: "menu"; description: workspace.collapsed ? "Expand navigation" : "Collapse navigation"; onClicked: workspace.collapsed=!workspace.collapsed }
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
                            Label { textFormat: Text.PlainText; text: nav.modelData.name; anchors.left: glyph.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; opacity: workspace.expansion }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                ToolButton {
                    id: aboutNavigation; Layout.fillWidth: true; Layout.preferredHeight: 48; padding: 0
                    Accessible.name: "About ExtraPane"; onClicked: workspace.page=3
                    ToolTip.visible: hovered && workspace.collapsed; ToolTip.text: "About ExtraPane"
                    background: Rectangle { radius: 10; color: workspace.page===3 ? "#192e4b" : aboutNavigation.hovered ? "#132136" : "transparent" }
                    contentItem: Item {
                        AppIcon { id: aboutGlyph; symbol: "about"; width: 22; height: 22; anchors.verticalCenter: parent.verticalCenter; x: 14*workspace.expansion+(parent.width-width)/2*(1-workspace.expansion) }
                        Label { textFormat: Text.PlainText; text: "About ExtraPane"; anchors.left: aboutGlyph.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; opacity: workspace.expansion }
                    }
                }
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
                Label { textFormat: Text.PlainText; text: ["Devices","Share","Connections","ExtraPane"][workspace.page]; font.pixelSize: 30; font.weight: Font.DemiBold }
                Label { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aabbd3"; text: workspace.page===0 ? "Use resources from another device. Connect over your local network or enter a reachable address." : workspace.page===1 ? "Choose what other devices can use. Sharing a resource does not activate it until someone connects." : workspace.page===2 ? "Each resource has its own connection. Stop one without interrupting the others." : "Displays, cameras, microphones, speakers and USB devices across your computers." }
                ColumnLayout {
                    visible: workspace.page===0; Layout.fillWidth: true; spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        Field { id: host; objectName: "peerAddress"; onTextEdited: workspace.expectedDeviceId=""; Layout.fillWidth: true; placeholderText: "Address or localhost"; Accessible.name: "Device address" }
                        Field { id: port; onTextEdited: workspace.expectedDeviceId=""; Layout.preferredWidth: 100; text: "43824"; placeholderText: "Port"; validator: IntValidator { bottom: 1; top: 65535 } }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Field { id: code; objectName: "peerCode"; Layout.fillWidth: true; placeholderText: "Pairing code (first connection)"; maximumLength: 6; echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly }
                        Action { objectName: "peerConnect"; text: "Connect"; onClicked: {
                            const numeric=workspace.checkFields([port]); host.invalid=!workspace.validAddress(host.text)
                            let remembered=false; let matches=0
                            for(const device of peers.remembered) {
                                if(workspace.expectedDeviceId && device.id===workspace.expectedDeviceId) remembered=true
                                if(device.address===(host.text.trim().toLowerCase()==="localhost" ? "127.0.0.1" : host.text.trim()) && Number(device.port)===Number(port.text)) matches++
                            }
                            if(!workspace.expectedDeviceId && matches>1 && !openConnection.checked) { notification.text="Choose a paired device below; this address matches more than one device."; notification.open(); return }
                            if(!workspace.expectedDeviceId && matches===1) remembered=true
                            code.invalid=!openConnection.checked && (!remembered || code.text.length>0) && !/^\d{6}$/.test(code.text)
                            if(!numeric||host.invalid||code.invalid) { notification.text="Check the highlighted information."; notification.open(); return }
                            peers.connectDevice(host.text,Number(port.text),code.text,workspace.expectedDeviceId,outgoingNic.selectedAddress,openConnection.checked); code.clear()
                        } }
                    }
                    ComboBox {
                        id: outgoingNic; objectName: "peerOutgoingNic"; Layout.fillWidth: true
                        textRole: "label"; valueRole: "address"
                        property string selectedAddress: ""
                        model: {
                            let rows=[{label:"Automatic route",address:""}].concat(peers.networkAddresses)
                            if(selectedAddress && !rows.some(row=>row.address===selectedAddress)) rows.push({label:selectedAddress+" (unavailable)",address:selectedAddress})
                            return rows
                        }
                        onActivated: selectedAddress=currentValue||""
                        onModelChanged: Qt.callLater(function() { outgoingNic.currentIndex=outgoingNic.indexOfValue(outgoingNic.selectedAddress) })
                        Accessible.name: "Connect through local interface"
                    }
                    CheckBox { id: openConnection; text: "Connect without pairing"; checked: false }
                    Label { textFormat: Text.PlainText; visible: openConnection.checked; text: "The other device must also have pairing disabled. Its identity will not be verified."; color: "#f0c66e"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout {
                        Label { textFormat: Text.PlainText; text: "Nearby devices"; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Button { text: peers.directory.scanning ? "Refresh" : "Find devices"; onClicked: { if (peers.directory.scanning) peers.directory.refresh(); else peers.directory.start() } }
                    }
                    Label { textFormat: Text.PlainText; text: peers.directory.status; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: peers.directory.devices
                        delegate: ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.name+" - "+modelData.address+":"+modelData.port
                            contentItem: Label { textFormat: Text.PlainText; text: parent.text; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                            onClicked: { host.text=modelData.address; port.text=String(modelData.port); workspace.expectedDeviceId=modelData.id; code.forceActiveFocus() }
                        }
                    }
                    Label { textFormat: Text.PlainText; visible: peers.remembered.length>0; text: "Paired devices"; font.weight: Font.DemiBold }
                    Repeater {
                        model: peers.remembered
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { textFormat: Text.PlainText; text: modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { textFormat: Text.PlainText; text: modelData.address ? modelData.address+":"+modelData.port : "Choose this device from discovery or enter its address"; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                            Action { objectName: "peerRemembered_"+modelData.id; text: "Connect"; enabled: !!modelData.address && modelData.port>0; onClicked: peers.connectDevice(modelData.address,modelData.port,"",modelData.id,outgoingNic.selectedAddress,false) }
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
                                Label { textFormat: Text.PlainText; text: modelData.name+(modelData.verified ? " (verified)" : " (unverified)"); font.pixelSize: 18; Layout.fillWidth: true; elide: Text.ElideRight }
                                Action { text: "Disconnect"; destructive: true; onClicked: peers.disconnectDevice(modelData.connection) }
                            }
                            Repeater {
                                model: workspace.resourceGroups
                                delegate: ResourceGroup {
                                    id: catalogGroup
                                    required property var modelData
                                    property var items: workspace.groupedRows(peerDevice.modelData.resources, modelData)
                                    title: modelData.title; symbol: modelData.icon; count: items.length
                                    groupKey: "peerCatalog_"+peerDevice.modelData.connection+"_"+modelData.key
                                    visible: count>0
                                    Repeater {
                                        model: catalogGroup.items
                                        delegate: Rectangle {
                                            id: resourceCard
                                            required property var modelData
                                            objectName: "peerResource_"+modelData.id
                                            property string connectionId: peerDevice.modelData.connection
                                        property var sessions: peers.connections.filter(row => row.connection===connectionId && row.owner===modelData.owner && row.resource===modelData.id)
                                            Layout.fillWidth: true; implicitHeight: resourceContent.implicitHeight+28; radius: 12; color: "#162339"
                                            ColumnLayout {
                                                id: resourceContent; anchors.fill: parent; anchors.margins: 14; spacing: 10
                                                Label { textFormat: Text.PlainText; text: resourceCard.modelData.label; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                                Label { text: resourceCard.modelData.kindId===0 ? "Virtual monitor destination" : "Existing screen (view only)"; visible: resourceCard.modelData.kindId===0 || resourceCard.modelData.kindId===5; color: "#aabbd3"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                                Label { textFormat: Text.PlainText; visible: resourceCard.modelData.delegated===true; text: "Via gateway ? "+(resourceCard.modelData.ownerName||""); color: "#aabbd3"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                                Label { textFormat: Text.PlainText; visible: !resourceCard.modelData.available; text: resourceCard.modelData.reason; color: "#f0c66e"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                                CheckBox { id: input; visible: resourceCard.modelData.inputAllowed; checked: resourceCard.modelData.inputAllowed&&resourceCard.modelData.kindId===0; text: "Allow input" }
                                                RowLayout {
                                                    visible: resourceCard.modelData.kindId===0
                                                    Field { id: modeWidth; objectName: "peerWidth_"+resourceCard.modelData.id; text: String(resourceCard.modelData.width||1920); placeholderText: "Width"; Layout.fillWidth: true; validator: IntValidator { bottom:64; top:4096 } }
                                                    Field { id: modeHeight; text: String(resourceCard.modelData.height||1080); placeholderText: "Height"; Layout.fillWidth: true; validator: IntValidator { bottom:64; top:4096 } }
                                                    Field { id: modeRefresh; text: String(Math.min(resourceCard.modelData.refresh||60,360)); placeholderText: "Hz / FPS"; Layout.preferredWidth: 90; validator: IntValidator { bottom:1; top:360 } }
                                                }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Label { text: resourceCard.modelData.kind; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                                    PowerButton {
                                                        objectName: "peerUse_"+resourceCard.modelData.id
                                                        destructive: resourceCard.sessions.length>0
                                                        description: destructive ? "Disconnect "+resourceCard.modelData.label+(resourceCard.sessions.length>1 ? " ("+resourceCard.sessions.length+" connections)" : "") : "Connect "+resourceCard.modelData.label+" - "+resourceCard.modelData.kind
                                                        enabled: destructive || resourceCard.modelData.available
                                                        onClicked: {
                                                            const current=resourceCard.sessions.slice()
                                                            if(current.length>0) {
                                                                for(const row of current) { if(row.phase==="connecting") peers.cancelResource(row.connection,row.request); else peers.stopResource(row.connection,row.grant) }
                                                                return
                                                            }
                                                            if(resourceCard.modelData.kindId===0&&!workspace.checkFields([modeWidth,modeHeight,modeRefresh])) return
                                                            peers.useResource(resourceCard.connectionId,resourceCard.modelData.owner,resourceCard.modelData.id,resourceCard.modelData.inputAllowed&&input.checked,resourceCard.modelData.kindId===0?Number(modeWidth.text):0,resourceCard.modelData.kindId===0?Number(modeHeight.text):0,resourceCard.modelData.kindId===0?Number(modeRefresh.text):0)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: workspace.page===1; Layout.fillWidth: true; spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        Label { textFormat: Text.PlainText; text: peers.deviceName; font.pixelSize: 20; Layout.fillWidth: true }
                        Button { objectName: "peerRefreshOffers"; text: "Refresh resources"; onClicked: peers.refreshOffers() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox { id: bind; objectName: "peerBindNic"; property bool invalid: false; Rectangle { anchors.fill: parent; color: "transparent"; radius: 5; border.width: 2; border.color: "#ff6978"; visible: bind.invalid } Timer { id: bindInvalidTimer; interval: 4500; onTriggered: bind.invalid=false } Layout.fillWidth: true; editable: true; model: peers.networkAddresses; textRole: "label"; valueRole: "address"; enabled: !peers.sharing; property string selectedAddress: ""
                            onActivated: selectedAddress=currentValue
                            onModelChanged: Qt.callLater(function() { if(!bind.selectedAddress&&bind.count) bind.selectedAddress=bind.valueAt(0); bind.currentIndex=bind.indexOfValue(bind.selectedAddress); bind.editText=bind.selectedAddress })
                            Connections { target: bind.contentItem; function onTextEdited() { bind.selectedAddress=bind.contentItem.text } }
                         }
                        Field { id: listenPort; text: "43824"; Layout.preferredWidth: 100; placeholderText: "Port"; enabled: !peers.sharing; validator: IntValidator { bottom:1; top:65535 } }
                    }
                    CheckBox { text: "Require pairing"; checked: peers.pairingRequired; onToggled: peers.pairingRequired=checked }
                    Label { textFormat: Text.PlainText; visible: peers.pairingRequired; text: "Pairing code: "+peers.pairingCode; font.pixelSize: 24; font.letterSpacing: 2 }
                    Label { textFormat: Text.PlainText; visible: peers.pairingRequired; text: "The code stays the same until ExtraPane restarts. Previously paired devices use their saved identity."; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: workspace.resourceGroups
                        delegate: ResourceGroup {
                            id: offerGroup
                            required property var modelData
                            property var items: workspace.groupedRows(peers.offers, modelData)
                            title: modelData.title; symbol: modelData.icon; count: items.length
                            groupKey: "peerOffers_"+modelData.key; visible: count>0
                            Repeater {
                                model: offerGroup.items
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true; spacing: 2
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 12
                                        Label { text: modelData.label; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; opacity: modelData.available ? 1 : 0.6 }
                                        PowerButton {
                                            objectName: "peerOffer_"+modelData.id
                                            destructive: modelData.enabled; enabled: modelData.available || modelData.enabled
                                            description: (modelData.enabled ? "Stop offering " : "Offer ")+modelData.label
                                            onClicked: peers.enableOffer(modelData.id,!modelData.enabled)
                                        }
                                    }
                                    Label { visible: modelData.kindId===0 || modelData.kindId===5; text: modelData.kindId===0 ? "Virtual monitor destination" : "Existing screen (view only)"; color: "#aabbd3"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Label { visible: !modelData.available; text: modelData.reason||"Unavailable"; color: "#f0c66e"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                    }
                    CheckBox { text: "Allow paired devices to use my paired devices"; checked: peers.gatewayEnabled; onToggled: peers.gatewayEnabled=checked }
                    Label { textFormat: Text.PlainText; visible: peers.gatewayEnabled; text: "ExtraPane relays permitted resources through this computer. Turning this off ends relayed connections. It does not open ports on your router."; color: "#aabbd3"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Action { text: peers.sharing ? "Stop sharing" : "Start sharing"; destructive: peers.sharing; onClicked: { if (peers.sharing) peers.stopSharing(); else { const valid=workspace.checkFields([listenPort]); bind.invalid=!workspace.validAddress(bind.selectedAddress); if(bind.invalid) bindInvalidTimer.restart(); if(!valid||bind.invalid) { notification.text="Check the highlighted information."; notification.open(); return } peers.startSharing(bind.selectedAddress,Number(listenPort.text)) } } }
                }
                ColumnLayout {
                    visible: workspace.page===2; Layout.fillWidth: true; spacing: 14
                    Label { textFormat: Text.PlainText; visible: peers.connections.length===0; text: "No resources connected"; color: "#aabbd3" }
                    Action { visible: peers.connections.length>0; text: "Disconnect all"; destructive: true; onClicked: peers.disconnectAll() }
                    Repeater {
                        model: workspace.resourceGroups
                        delegate: ResourceGroup {
                            id: connectionGroup
                            required property var modelData
                            property var items: workspace.groupedRows(peers.connections, modelData)
                            title: modelData.title; symbol: modelData.icon; count: items.length
                            groupKey: "peerConnections_"+modelData.key; visible: count>0
                            Repeater {
                                model: connectionGroup.items
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: (modelData.label||modelData.kind||"Shared resource")+(modelData.phase==="reconnecting" ? " - Reconnecting..." : modelData.phase==="connecting" ? " - Connecting..." : ""); wrapMode: Text.WordWrap }
                                    PowerButton { objectName: "peerStop_"+modelData.grant; description: (modelData.phase==="connecting" ? "Cancel " : "Disconnect ")+(modelData.label||modelData.kind||"resource"); destructive: true; onClicked: { if(modelData.phase==="connecting") peers.cancelResource(modelData.connection,modelData.request); else peers.stopResource(modelData.connection,modelData.grant) } }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: workspace.page===3; Layout.fillWidth: true; spacing: 18
                    Image { source: "qrc:/extrapane.png"; Layout.preferredWidth: 96; Layout.preferredHeight: 96; Layout.alignment: Qt.AlignRight }
                    Label { textFormat: Text.PlainText; text: "Device-sharing development build"; color: "#aabbd3" }
                    Button { text: "Read the documentation"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane#readme") }
                    Button { text: "Releases and changelog"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane/releases") }
                    Button { text: "GitHub repository"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane") }
                }
                Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: peers.status; wrapMode: Text.WordWrap; color: "#aabbd3" }
            }
        }
    }
}
