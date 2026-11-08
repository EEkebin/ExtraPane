import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: workspace
    property int page: 0
    property bool collapsed: false
    property string expectedDeviceId: ""
    property bool manualExpanded: false
    function connectedDevice(identity) { return peers.devices.some(row => row.identity===identity && row.incoming!==true) }
    readonly property bool manualConnected: peers.devices.some(row => row.incoming!==true &&
        (workspace.expectedDeviceId ? row.identity===workspace.expectedDeviceId :
         row.address===(host.text.trim().toLowerCase()==="localhost" ? "127.0.0.1" : host.text.trim()) && Number(row.port)===Number(port.text))) ||
        peers.remembered.some(row => (workspace.expectedDeviceId ? row.id===workspace.expectedDeviceId :
            row.address===(host.text.trim().toLowerCase()==="localhost" ? "127.0.0.1" : host.text.trim()) && Number(row.port)===Number(port.text)) && connectedDevice(row.id))
    readonly property var newNearbyDevices: peers.directory.devices.filter(row =>
        !peers.remembered.some(saved => saved.id===row.id) && !peers.devices.some(device => device.identity===row.id))
    property var collapsedDevices: ({})
    function toggleDevice(key) { const next=Object.assign({},collapsedDevices); if(next[key]) delete next[key]; else next[key]=true; collapsedDevices=next }
    readonly property bool sharingBusy: peers.sharing || peers.sharingStarting
    readonly property bool hasSharedResources: peers.connections.some(row => row.incoming===true)
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
    function sharingConfigurationValid() {
        if(!sharingBusy) {
            const valid=checkFields([listenPort])
            bind.invalid=!validAddress(bind.selectedAddress)
            if(bind.invalid) bindInvalidTimer.restart()
            if(!valid || bind.invalid) {
                notification.text="Check the highlighted information."
                notification.open()
                return false
            }
        }
        return true
    }
    function setOfferSharing(id, enabled) {
        if(enabled && !sharingConfigurationValid()) return
        peers.setOfferSharing(id,enabled,bind.selectedAddress,Number(listenPort.text))
    }
    function setGatewaySharing(enabled) {
        if(enabled && !sharingConfigurationValid()) return
        peers.setGatewaySharing(enabled,bind.selectedAddress,Number(listenPort.text))
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
        let grouped=[]
        for(const kind of group.kinds) grouped=grouped.concat((rows||[]).filter(row => row.kindId===kind))
        return grouped
    }
    function startsMonitorSection(rows,index) {
        if(!rows || !rows[index]) return false
        const kind=rows[index].kindId
        return (kind===0 || kind===5) && (index===0 || rows[index-1].kindId!==kind)
    }
    component MonitorSection: Label {
        property int kind: 0
        property bool receiving: false
        text: receiving ? (kind===0 ? "Extend this desktop" : "View an existing screen (view only)") : (kind===0 ? "Use as an extra monitor" : "Share an existing screen (view only)")
        textFormat: Text.PlainText; font.weight: Font.DemiBold; color: "#aabbd3"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; topPadding: 8; bottomPadding: 4
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
                Label { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aabbd3"; text: workspace.page===0 ? "Pair a new device or manage your saved devices. Search nearby or connect by address." : workspace.page===1 ? "Choose what other devices can use. Sharing a resource does not activate it until someone connects." : workspace.page===2 ? "Choose resources from your connected devices. Each power button connects or disconnects only that resource." : "Displays, cameras, microphones, speakers and USB devices across your computers." }
                ColumnLayout {
                    visible: workspace.page===0; Layout.fillWidth: true; spacing: 14
                    RowLayout {
                        Label { textFormat: Text.PlainText; text: "Nearby devices"; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Button { objectName: "peerRefreshDiscovery"; text: peers.directory.scanning ? "Searching..." : "Search nearby"; enabled: !peers.directory.scanning; onClicked: peers.startDiscovery() }
                    }
                    Label { textFormat: Text.PlainText; text: peers.directory.status; visible: text.length>0; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: workspace.newNearbyDevices
                        delegate: ItemDelegate {
                            required property var modelData
                            objectName: "peerNearby_"+modelData.id
                            Layout.fillWidth: true
                            text: modelData.name+" - "+modelData.address+":"+modelData.port
                            contentItem: Label { textFormat: Text.PlainText; text: parent.text; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                            onClicked: { workspace.manualExpanded=true; host.text=modelData.address; port.text=String(modelData.port); workspace.expectedDeviceId=modelData.id; code.clear(); openConnection.checked=false; code.forceActiveFocus() }
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
                            Action { objectName: "peerRemembered_"+modelData.id; text: "Connect"; visible: !workspace.connectedDevice(modelData.id); enabled: !!modelData.address && modelData.port>0; onClicked: peers.connectDevice(modelData.address,modelData.port,"",modelData.id,outgoingNic.selectedAddress,false) }
                            Label { text: "Connected"; color: "#aabbd3"; visible: workspace.connectedDevice(modelData.id) }
                            Action { text: "Resources"; visible: workspace.connectedDevice(modelData.id); onClicked: workspace.page=2 }
                            Action { objectName: "peerForget_"+modelData.id; text: "Forget"; destructive: true; onClicked: peers.forgetDevice(modelData.id) }
                        }
                    }
                    ToolButton {
                        objectName: "peerManualHeader"
                        Layout.fillWidth: true; padding: 14
                        text: "Connect by address"
                        Accessible.name: text+(workspace.manualExpanded ? ", expanded" : ", collapsed")
                        onClicked: workspace.manualExpanded=!workspace.manualExpanded
                        background: Rectangle { radius: 10; color: parent.hovered ? "#20354f" : "#192b43" }
                        contentItem: RowLayout {
                            Label { text: "Connect by address"; font.weight: Font.DemiBold; Layout.fillWidth: true }
                            AppIcon { symbol: "chevron"; width: 18; height: 18; rotation: workspace.manualExpanded ? 90 : 0 }
                        }
                    }
                    ColumnLayout {
                        objectName: "peerManualFields"
                        visible: workspace.manualExpanded; Layout.fillWidth: true; spacing: 14
                        Label { text: "Use a local, static or forwarded address when the device is not listed nearby."; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Field { id: host; objectName: "peerAddress"; onTextEdited: workspace.expectedDeviceId=""; Layout.fillWidth: true; placeholderText: "Address or localhost"; Accessible.name: "Device address" }
                            Field { id: port; objectName: "peerPort"; onTextEdited: workspace.expectedDeviceId=""; Layout.preferredWidth: 100; text: "43824"; placeholderText: "Port"; validator: IntValidator { bottom: 1; top: 65535 } }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Field { id: code; objectName: "peerCode"; Layout.fillWidth: true; placeholderText: "Pairing code (first connection)"; maximumLength: 6; echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly }
                            Action { objectName: "peerConnect"; text: "Connect"; visible: !workspace.manualConnected; onClicked: {
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
                    }
                    Label { text: "Connected devices"; font.weight: Font.DemiBold; visible: peers.devices.length>0 }
                    Repeater {
                        model: peers.devices
                        delegate: Frame {
                            required property var modelData
                            Layout.fillWidth: true; padding: 12
                            background: Rectangle { radius: 12; color: "#111f32"; border.color: "#48617e" }
                            contentItem: RowLayout {
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelData.name; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: modelData.verified ? "Connected - verified" : "Connected - unverified"; color: "#aabbd3" }
                                }
                                Action { text: "Resources"; visible: !modelData.incoming; onClicked: workspace.page=2 }
                                Action { text: "Disconnect"; destructive: true; onClicked: peers.disconnectDevice(modelData.connection) }
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
                        ComboBox { id: bind; objectName: "peerBindNic"; property bool invalid: false; Rectangle { anchors.fill: parent; color: "transparent"; radius: 5; border.width: 2; border.color: "#ff6978"; visible: bind.invalid } Timer { id: bindInvalidTimer; interval: 4500; onTriggered: bind.invalid=false } Layout.fillWidth: true; editable: true; model: peers.networkAddresses; textRole: "label"; valueRole: "address"; enabled: peers.connections.length===0; property string selectedAddress: ""
                            onActivated: selectedAddress=currentValue
                            onModelChanged: Qt.callLater(function() { if(!bind.selectedAddress&&bind.count) bind.selectedAddress=bind.valueAt(0); bind.currentIndex=bind.indexOfValue(bind.selectedAddress); bind.editText=bind.selectedAddress })
                            Connections { target: bind.contentItem; function onTextEdited() { bind.selectedAddress=bind.contentItem.text } }
                         }
                        Field { id: listenPort; objectName: "peerListenPort"; text: "43824"; Layout.preferredWidth: 100; placeholderText: "Port"; enabled: peers.connections.length===0; validator: IntValidator { bottom:1; top:65535 } }
                    }
                    Action { text: workspace.sharingBusy ? "Apply address" : "Retry availability"; enabled: peers.connections.length===0; onClicked: { if(workspace.sharingConfigurationValid()) peers.startAvailableSharing(bind.selectedAddress,Number(listenPort.text)) } }
                    Label { text: "Available resources can be used from your paired devices. A red power button disconnects an active connection."; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                                    id: offerRow
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true; spacing: 2
                                    MonitorSection { objectName: offerGroup.groupKey+"Section_"+offerRow.index; kind: offerRow.modelData.kindId; visible: workspace.startsMonitorSection(offerGroup.items,offerRow.index) }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 12
                                        Label { text: modelData.label; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; opacity: modelData.available ? 1 : 0.6 }
                                        PowerButton {
                                            objectName: "peerOffer_"+modelData.id
                                            destructive: modelData.inUse===true; enabled: modelData.inUse===true
                                            description: (modelData.inUse ? "Disconnect " : "Available to paired devices: ")+modelData.label
                                            onClicked: { if(modelData.inUse) peers.stopIncomingResource(modelData.id) }
                                        }
                                    }
                                    Label { visible: modelData.inUse===true; textFormat: Text.PlainText; text: modelData.users && modelData.users.length ? "Connected to "+modelData.users.join(", ") : String(modelData.sessionCount||1)+" active connection(s)"; color: "#9eacc2"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Label { visible: !modelData.available; text: modelData.reason||"Unavailable"; color: "#f0c66e"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                    }
                    CheckBox { objectName: "peerGatewaySharing"; text: "Allow paired devices to use my paired devices"; checked: peers.gatewayEnabled; onToggled: { workspace.setGatewaySharing(checked); checked=Qt.binding(function() { return peers.gatewayEnabled }) } }
                    Label { textFormat: Text.PlainText; visible: peers.gatewayEnabled; text: "ExtraPane relays permitted resources through this computer. Turning this off ends relayed connections. It does not open ports on your router."; color: "#aabbd3"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Action { objectName: "peerStopSharingAll"; text: "Disconnect shared resources"; destructive: true; visible: workspace.hasSharedResources; onClicked: peers.disconnectIncomingAll() }
                }
                ColumnLayout {
                    visible: workspace.page===2; Layout.fillWidth: true; spacing: 14
                    Label { text: "Connect a device on the Devices page to see its resources here."; visible: !peers.devices.some(row => row.incoming!==true); wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#aabbd3" }
                    Repeater {
                        model: peers.devices
                        delegate: Frame {
                            id: peerDevice
                            required property var modelData
                            visible: !modelData.incoming; Layout.fillWidth: true
                            padding: 12
                            background: Rectangle {
                                radius: 16
                                color: "#111f32"
                                border.width: 1
                                border.color: "#48617e"
                            }
                            readonly property string deviceKey: JSON.stringify([modelData.identity||"",String(modelData.connection)])
                            readonly property bool expanded: !workspace.collapsedDevices[deviceKey]
                            readonly property int activeCount: peers.connections.filter(row => row.incoming!==true && row.connection===modelData.connection).length
                            contentItem: ColumnLayout {
                                spacing: 10
                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                ToolButton {
                                    objectName: "peerDeviceHeader_"+peerDevice.modelData.connection
                                    Layout.fillWidth: true; implicitHeight: 64; padding: 12
                                    Accessible.name: (peerDevice.expanded ? "Collapse " : "Expand ")+peerDevice.modelData.name
                                    Accessible.description: peerDevice.expanded ? "Resources expanded" : "Resources collapsed"
                                    onClicked: workspace.toggleDevice(peerDevice.deviceKey)
                                    background: Rectangle { radius: 12; color: parent.hovered ? "#243a57" : "#192d46" }
                                    contentItem: RowLayout {
                                        spacing: 12
                                        AppIcon { symbol: "chevron"; Layout.preferredWidth: 18; Layout.preferredHeight: 18; rotation: peerDevice.expanded ? 90 : 0 }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 3
                                            Label { textFormat: Text.PlainText; text: peerDevice.modelData.name+(peerDevice.modelData.verified ? " (verified)" : " (unverified)"); font.pixelSize: 18; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Label { text: peerDevice.modelData.resources.length+" resources"+(peerDevice.activeCount ? " - "+peerDevice.activeCount+" active" : ""); color: "#9eacc2"; Layout.fillWidth: true; elide: Text.ElideRight }
                                        }
                                    }
                                }
                                Action { objectName: "peerDeviceDisconnect_"+peerDevice.modelData.connection; text: "Disconnect"; destructive: true; onClicked: peers.disconnectDevice(peerDevice.modelData.connection) }
                            }
                            ColumnLayout {
                                objectName: "peerDeviceBody_"+peerDevice.modelData.connection
                                Layout.fillWidth: true; spacing: 10; visible: peerDevice.expanded
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
                                        delegate: ColumnLayout {
                                            id: catalogRow
                                            required property var modelData
                                            required property int index
                                            Layout.fillWidth: true; spacing: 8
                                            MonitorSection { objectName: catalogGroup.groupKey+"Section_"+catalogRow.index; kind: catalogRow.modelData.kindId; receiving: true; visible: workspace.startsMonitorSection(catalogGroup.items,catalogRow.index) }
                                            Rectangle {
                                            id: resourceCard
                                            property var modelData: catalogRow.modelData
                                            objectName: "peerResource_"+modelData.id
                                            property string connectionId: peerDevice.modelData.connection
                                            property var sessions: peers.connections.filter(row => row.incoming!==true && row.connection===connectionId && row.owner===modelData.owner && row.resource===modelData.id)
                                            Layout.fillWidth: true; implicitHeight: resourceContent.implicitHeight+28; radius: 12; color: "#162339"
                                            ColumnLayout {
                                                id: resourceContent; anchors.fill: parent; anchors.margins: 14; spacing: 10
                                                Label { textFormat: Text.PlainText; text: resourceCard.modelData.label; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
                                                    Item { Layout.fillWidth: true }
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
                    }
                    }
                    Label { text: "Active connections"; visible: peers.connections.length>0; font.weight: Font.DemiBold }
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
                    Label { textFormat: Text.PlainText; objectName: "peerRunningVersion"; text: "Running version: "+(typeof receiver!=="undefined" ? receiver.version : Qt.application.version); color: "#aabbd3" }
                    Button { text: "Read the documentation"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane#readme") }
                    Button { text: "Releases and changelog"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane/releases") }
                    Button { text: "GitHub repository"; onClicked: Qt.openUrlExternally("https://github.com/EEkebin/ExtraPane") }
                }
                Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: peers.status; wrapMode: Text.WordWrap; color: "#aabbd3" }
            }
        }
    }
}
