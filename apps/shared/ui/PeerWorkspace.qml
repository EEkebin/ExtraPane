import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: workspace
    property int page: 0
    onPageChanged: if(pageScroll.contentItem) pageScroll.contentItem.contentY=0
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
    property bool localExpanded: false
    property var expandedGroups: ({})
    function toggleGroup(key) { const next=Object.assign({},expandedGroups);next[key]=!next[key];expandedGroups=next }
    property var collapsedDevices: ({})
    function toggleDevice(key) { const next=Object.assign({},collapsedDevices); next[key]=!next[key]; collapsedDevices=next }
    readonly property var managedDevices: {
        const byIdentity={}
        for(const row of peers.devices) {
            const key=row.identity||String(row.connection)
            if(!byIdentity[key] || row.incoming!==true) byIdentity[key]=row
        }
        return Object.values(byIdentity).filter(row=>row.verified!==true&&!peers.remembered.some(saved=>saved.id===row.identity))
    }
    function disconnectManagedDevice(device) {
        for(const row of peers.devices.slice())
            if(device.identity ? row.identity===device.identity : row.connection===device.connection)
                peers.disconnectDevice(row.connection)
    }
    readonly property bool sharingBusy: peers.sharing || peers.sharingStarting
    readonly property bool hasSharedResources: peers.connections.some(row => row.incoming===true)
    property real expansion: collapsed ? 0 : 1
    Behavior on expansion { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Connections { target: peers; function onToast(message) { notification.text = message; notification.open() } }
    Popup {
        id: notification
        objectName: "peerNotification"
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
        const valid=portMode.currentIndex===0 || checkFields([listenPort])
        customBind.invalid=!(bind.selectedAddress==="0.0.0.0" || validAddress(bind.selectedAddress))
        if(!valid || customBind.invalid) {
            notification.text="Check the highlighted information."; notification.open(); return false
        }
        return true
    }
    function listeningPort() { return portMode.currentIndex===0 ? 43824 : Number(listenPort.text) }
    function applyListening() { if(sharingConfigurationValid()) peers.startAvailableSharing(bind.selectedAddress,listeningPort()) }
    function setOfferSharing(id, enabled) {
        if(enabled && !sharingConfigurationValid()) return
        peers.setOfferSharing(id,enabled,bind.selectedAddress,listeningPort())
    }
    function setGatewaySharing(enabled) {
        if(enabled && !sharingConfigurationValid()) return
        peers.setGatewaySharing(enabled,bind.selectedAddress,listeningPort())
    }
    Dialog {
        id: pendingRemovalDialog
        property string deviceId: ""
        title: "Clear pending removal?"
        modal: true; anchors.centerIn: parent; width: Math.min(parent.width-40,460); height: 230
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "Removal on the other device has not been confirmed. Forget this computer on that device manually before pairing again."; wrapMode: Text.WordWrap }
        onAccepted: peers.dismissPendingForget(deviceId)
    }
    function beginConnection(address,targetPort,identity,openMode) {
        if(pairingDialog.visible) return
        const normalized=address.trim().toLowerCase()==="localhost" ? "127.0.0.1" : address.trim()
        if(peers.remembered.some(device=>device.pendingForget&&(identity?device.id===identity:device.address===normalized&&Number(device.port)===Number(targetPort)))) { notification.text="Finish or clear the pending pairing removal before pairing again."; notification.open(); return }
        const matches=peers.remembered.filter(device=>!device.pendingForget && (identity ? device.id===identity : device.address===normalized&&Number(device.port)===Number(targetPort)))
        if(matches.length>1) { notification.text="This address matches several paired devices. Use the device's current address."; notification.open(); return }
        const known=matches.length===1
        const route=outgoingNic.selectedAddress
        if(route&&!peers.networkAddresses.some(item=>item.address===route)){notification.text="Choose an available local network interface.";notification.open();return}
        const discovered=peers.directory.devices.find(device=>identity?device.id===identity:device.address===normalized&&Number(device.port)===Number(targetPort))
        const snapshot={name:known?matches[0].name:(discovered&&discovered.name?discovered.name:"device"),subtitle:address.trim()+":"+Number(targetPort),address:address.trim(),port:Number(targetPort),identity:known?matches[0].id:identity,route:route,open:known?false:openMode}
        if(known||snapshot.open){peers.connectDevice(snapshot.address,snapshot.port,"",snapshot.identity,snapshot.route,snapshot.open);return}
        pairingDialog.target=snapshot; pairingDialog.submitted=false; code.clear();code.invalid=false;pairingDialog.open()
    }
    function beginGatewayPairing(connection,identity,name) {
        if(pairingDialog.visible) return
        const saved=peers.remembered.find(device=>device.id===identity)
        if(saved&&saved.pendingForget){notification.text="Finish or clear the pending pairing removal before pairing again.";notification.open();return}
        if(saved){peers.connectVia(String(connection),identity,"");return}
        const gateway=peers.devices.find(device=>String(device.connection)===String(connection))
        const route=gateway&&gateway.address ? " ("+gateway.address+":"+gateway.port+")" : ""
        pairingDialog.target={gateway:String(connection),identity:identity,name:name||"device",subtitle:"Via "+(gateway&&gateway.name?gateway.name:"gateway")+route}
        pairingDialog.submitted=false;code.clear();code.invalid=false;pairingDialog.open()
    }
    function gatewayDevices(resources) {
        const devices={}
        for(const item of resources) if(item.delegated===true&&!peers.remembered.some(saved=>saved.id===item.owner&&!saved.pendingForget))
            devices[item.owner]={identity:item.owner,name:item.ownerName||"Device"}
        return Object.values(devices)
    }
    Dialog {
        id: pairingDialog; objectName: "peerPairingDialog"
        property var target: ({})
        property bool submitted: false
        title: "Pair with "+(target.name||"device"); modal: true; anchors.centerIn: parent
        Material.theme: Material.Dark; Material.accent: "#80b8ff"
        padding: 24
        background: Rectangle { color: "#14243a"; radius: 16; border.color: "#48617e" }
        Overlay.modal: Rectangle { color: "#99050910" }
        header: ColumnLayout { spacing:6
            Label { objectName:"peerPairingTitle"; text:pairingDialog.title;textFormat:Text.PlainText;color:"#eef4ff";font.pixelSize:22;Layout.fillWidth:true;Layout.leftMargin:24;Layout.rightMargin:24;Layout.topMargin:22;elide:Text.ElideRight }
            Label { objectName:"peerPairingSubtitle";text:pairingDialog.target.subtitle||"";textFormat:Text.PlainText;color:"#aabbd3";font.pixelSize:13;Layout.fillWidth:true;Layout.leftMargin:24;Layout.rightMargin:24;Layout.bottomMargin:14;elide:Text.ElideRight }
        }
        width: Math.min(workspace.width-40,440); height: 320
        closePolicy: Popup.CloseOnEscape
        onOpened: code.forceActiveFocus()
        onClosed: { code.clear();code.invalid=false;target=({});submitted=false }
        function submit() {
            if(submitted||!visible||(!target.address&&!target.gateway))return
            code.invalid=!/^\d{6}$/.test(code.text)
            if(code.invalid){code.forceActiveFocus();return}
            submitted=true
            const frozen=target
            if(frozen.gateway) peers.connectVia(frozen.gateway,frozen.identity,code.text)
            else peers.connectDevice(frozen.address,frozen.port,code.text,frozen.identity,frozen.route,false)
            close()
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label { text:"Pairing Code";color:"#eef4ff";Layout.fillWidth:true }
            Field { id: code; objectName: "peerCode"; Layout.fillWidth: true; Layout.preferredHeight:52; Accessible.name: "Pairing Code"; maximumLength: 6; echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; onAccepted: pairingDialog.submit()
                leftPadding:14;rightPadding:14;topPadding:10;bottomPadding:10
                background:Rectangle { color:"#111f32";radius:6;border.width:code.activeFocus||code.invalid?2:1;border.color:code.invalid?"#ff6978":code.activeFocus?"#80b8ff":"#48617e" }
                Text { text:"XXXXXX";visible:code.text.length===0;color:"#9eacc2";anchors.left:parent.left;anchors.leftMargin:14;anchors.verticalCenter:parent.verticalCenter;font:code.font }
            }
            Label { text: "Enter exactly six digits."; color: "#ff6978"; visible: code.invalid }
            RowLayout { Layout.alignment: Qt.AlignRight
                Action { objectName:"peerPairingCancel"; text:"Cancel"; onClicked:pairingDialog.close() }
                Action { objectName:"peerPairingSubmit"; text:"Pair"; enabled:!pairingDialog.submitted; onClicked:pairingDialog.submit() }
            }
        }
    }
    property var monitorDrafts: []
    property int nextMonitorRow: 0
    function customMonitor(item) { return item.kindId===0 && ((!item.width&&!item.height) || /custom/i.test(item.label||"")) }
    function preferredMonitor(items) {
        return items.find(item => customMonitor(item)) || items.find(item => item.kindId===0) || items[0]
    }
    function addMonitorRow(connection,items) {
        const item=preferredMonitor(items); if(!item) return
        monitorDrafts=monitorDrafts.concat([{uid:String(++nextMonitorRow),connection:String(connection),owner:item.owner,resource:item.id,kindId:item.kindId,width:String(item.width||1920),height:String(item.height||1080),refresh:String(Math.min(item.refresh||60,360)),input:!!item.inputAllowed&&item.kindId===0,request:""}])
    }
    function ensureMonitorRow(connection,items) {
        if(!items.length) return
        const target=String(connection)
        let changed=false
        const next=monitorDrafts.map(row => {
            if(row.connection===target || !items.some(item=>item.owner===row.owner)) return row
            const active=peers.connections.some(session=>session.incoming!==true&&String(session.connection)===row.connection&&String(session.request)===row.request)
            const oldLink=peers.devices.some(device=>String(device.connection)===row.connection)
            if(active||oldLink) return row
            changed=true; return Object.assign({},row,{connection:target,request:""})
        })
        if(changed)monitorDrafts=next
        if(!monitorDrafts.some(row => row.connection===target)) addMonitorRow(connection,items)
    }
    function updateMonitorRow(uid,changes) {
        monitorDrafts=monitorDrafts.map(row => row.uid===uid ? Object.assign({},row,changes) : row)
    }
    function saveMonitorDraft(uid,changes) {
        const row=monitorDrafts.find(row=>row.uid===uid); if(row)Object.assign(row,changes)
    }
    function selectMonitor(uid,item) {
        if(!item) return
        updateMonitorRow(uid,{owner:item.owner,resource:item.id,kindId:item.kindId,width:String(item.width||1920),height:String(item.height||1080),refresh:String(Math.min(item.refresh||60,360)),input:!!item.inputAllowed&&item.kindId===0})
    }
    component MonitorRows: ColumnLayout {
        id: monitorList
        required property string connectionId
        required property var items
        Layout.fillWidth: true; spacing: 12
        Component.onCompleted: workspace.ensureMonitorRow(connectionId,items)
        onItemsChanged: Qt.callLater(function(){workspace.ensureMonitorRow(monitorList.connectionId,monitorList.items)})
        Action { objectName: monitorList.items.length ? "peerAddMonitor_"+monitorList.connectionId : ""; text: "+ Add monitor"; Layout.alignment: Qt.AlignRight; onClicked: workspace.addMonitorRow(monitorList.connectionId,monitorList.items) }
        Repeater {
            model: monitorList.items.length ? workspace.monitorDrafts.filter(row => row.connection===monitorList.connectionId) : []
            delegate: Frame {
                id: monitorRow
                required property var modelData
                readonly property var selected: monitorList.items.find(item => item.owner===modelData.owner && item.id===modelData.resource) || {id:modelData.resource,owner:modelData.owner,kindId:modelData.kindId,label:"Unavailable monitor",available:false,inputAllowed:false,reason:"This monitor is no longer available"}
                readonly property var choices: monitorList.items.filter(item=>item.kindId===modelData.kindId).map((item,index)=>Object.assign({},item,{choiceName:workspace.customMonitor(item) ? "Custom" : "Monitor "+(monitorList.items.filter(candidate=>candidate.kindId===modelData.kindId&&!workspace.customMonitor(candidate)).findIndex(candidate=>candidate.id===item.id&&candidate.owner===item.owner)+1)+" - "+item.label}))
                readonly property var sessions: peers.connections.filter(row => row.incoming!==true && String(row.connection)===monitorList.connectionId && modelData.request && String(row.request)===modelData.request)
                readonly property bool active: sessions.length>0
                readonly property bool occupied: modelData.kindId===5 && !active && peers.connections.some(row=>row.incoming!==true&&row.kindId===5&&row.owner===selected.owner&&row.resource===selected.id)
                Layout.fillWidth: true; padding: 12
                background: Rectangle { radius: 10; color: "#16283e"; border.color: "#344d68" }
                contentItem: ColumnLayout {
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox { objectName: "peerMonitorMode_"+monitorRow.modelData.uid; model:["Virtual monitor","View existing screen"]; currentIndex:monitorRow.modelData.kindId===5?1:0; enabled:!monitorRow.active; Layout.preferredWidth:180; onActivated:{const kind=index===1?5:0;const selected=monitorList.items.find(item=>item.kindId===kind);if(selected)workspace.selectMonitor(monitorRow.modelData.uid,selected);else{notification.text="This device has no monitors in that mode.";notification.open();currentIndex=monitorRow.modelData.kindId===5?1:0}} }
                        ComboBox { objectName: "peerMonitorChoice_"+monitorRow.modelData.uid; Layout.fillWidth:true; textRole:"choiceName"; model:monitorRow.choices; currentIndex:monitorRow.choices.findIndex(item=>item.owner===monitorRow.selected.owner&&item.id===monitorRow.selected.id); enabled:!monitorRow.active; onActivated:workspace.selectMonitor(monitorRow.modelData.uid,monitorRow.choices[index]) }
                    }
                    Label { visible:!monitorRow.selected.available||monitorRow.occupied; text:monitorRow.occupied?"This screen is already being viewed.":(monitorRow.selected.reason||"Unavailable"); color:"#f0c66e"; wrapMode:Text.WordWrap; Layout.fillWidth:true }
                    RowLayout {
                        visible:monitorRow.modelData.kindId===0; Layout.fillWidth:true
                        Field { id: monitorWidth; objectName:"peerMonitorWidth_"+monitorRow.modelData.uid; Layout.fillWidth:true; placeholderText:"Width"; text:monitorRow.modelData.width; enabled:!monitorRow.active; validator:IntValidator{bottom:64;top:4096} onTextEdited:workspace.saveMonitorDraft(monitorRow.modelData.uid,{width:text}) }
                        Field { id: monitorHeight; objectName:"peerMonitorHeight_"+monitorRow.modelData.uid; Layout.fillWidth:true; placeholderText:"Height"; text:monitorRow.modelData.height; enabled:!monitorRow.active; validator:IntValidator{bottom:64;top:4096} onTextEdited:workspace.saveMonitorDraft(monitorRow.modelData.uid,{height:text}) }
                        Field { id: monitorRefresh; objectName:"peerMonitorRefresh_"+monitorRow.modelData.uid; Layout.preferredWidth:110; placeholderText:"Hz / FPS"; text:monitorRow.modelData.refresh; enabled:!monitorRow.active; validator:IntValidator{bottom:1;top:360} onTextEdited:workspace.saveMonitorDraft(monitorRow.modelData.uid,{refresh:text}) }
                    }
                    RowLayout {
                        Layout.fillWidth:true
                        CheckBox { id: monitorInput; objectName:"peerMonitorInput_"+monitorRow.modelData.uid; text:"Allow input"; visible:monitorRow.selected.inputAllowed; enabled:!monitorRow.active; checked:monitorRow.modelData.input; onToggled:workspace.saveMonitorDraft(monitorRow.modelData.uid,{input:checked}) }
                        Item { Layout.fillWidth:true }
                        Action { objectName:"peerRemoveMonitor_"+monitorRow.modelData.uid; text:"Remove"; enabled:!monitorRow.active; onClicked:workspace.monitorDrafts=workspace.monitorDrafts.filter(row=>row.uid!==monitorRow.modelData.uid) }
                        PowerButton {
                            objectName:"peerMonitorPower_"+monitorRow.modelData.uid; destructive:monitorRow.active; description:(destructive?"Disconnect ":"Connect ")+monitorRow.selected.label; enabled:monitorRow.active||(monitorRow.selected.available&&!monitorRow.occupied)
                            onClicked:{
                                const active=monitorRow.sessions.slice();if(active.length){for(const session of active){if(session.phase==="connecting")peers.cancelResource(session.connection,session.request);else peers.stopResource(session.connection,session.grant)}return}
                                if(monitorRow.modelData.kindId===0&&!workspace.checkFields([monitorWidth,monitorHeight,monitorRefresh]))return
                                const request=peers.useResource(monitorList.connectionId,monitorRow.selected.owner,monitorRow.selected.id,!!monitorRow.selected.inputAllowed&&monitorInput.checked,monitorRow.modelData.kindId===0?Number(monitorWidth.text):0,monitorRow.modelData.kindId===0?Number(monitorHeight.text):0,monitorRow.modelData.kindId===0?Number(monitorRefresh.text):0)
                                if(request)workspace.updateMonitorRow(monitorRow.modelData.uid,{request:String(request),width:monitorWidth.text,height:monitorHeight.text,refresh:monitorRefresh.text})
                            }
                        }
                    }
                }
            }
        }
    }
    component SectionHeading: ColumnLayout {
        property string title: ""
        Layout.fillWidth: true; spacing: 12
        Label { text: parent.title; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#2b3c51" }
    }
    component InfoLink: ItemDelegate {
        id: infoLink
        property string title: ""
        property string subtitle: ""
        property string destination: ""
        property string symbol: "link"
        Layout.fillWidth: true; implicitHeight: Math.max(78, contentItem.implicitHeight+32); padding: 16
        Accessible.name: title
        onClicked: Qt.openUrlExternally(destination)
        background: Rectangle { radius: 12; color: parent.hovered ? "#1c3048" : "#132237"; border.color: parent.activeFocus ? "#79b2ff" : "#30445f" }
        contentItem: RowLayout {
            spacing: 14
            AppIcon { symbol: infoLink.symbol; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            ColumnLayout { Layout.fillWidth: true; spacing: 4
                Label { text: infoLink.title; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: infoLink.subtitle; color: "#aabbd3"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            AppIcon { symbol: "link"; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
        }
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
        text: receiving ? (kind===0 ? "Extend this desktop" : "View an existing screen") : (kind===0 ? "Use as an extra monitor" : "Share an existing screen")
        textFormat: Text.PlainText; font.weight: Font.DemiBold; color: "#aabbd3"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; topPadding: 8; bottomPadding: 4
    }
    component ResourceGroup: Frame {
        id: group
        property string title: ""
        property string symbol: ""
        property int count: 0
        readonly property bool expanded: workspace.expandedGroups[stateKey]===true
        property string groupKey: ""
        property string stateKey: groupKey
        default property alias rows: groupBody.data
        Layout.fillWidth: true; padding: 12
        background: Rectangle { radius: 14; color: "#101e30"; border.color: "#405773"; border.width: 1 }
        contentItem: ColumnLayout {
        spacing: group.expanded ? 10 : 0
        ToolButton {
            id: groupHeader
            objectName: group.groupKey+"Header"
            Layout.fillWidth: true; Layout.preferredHeight: 54; padding: 14
            Accessible.name: group.title+", "+group.count+" resources, "+(group.expanded ? "expanded" : "collapsed")
            onClicked: workspace.toggleGroup(group.stateKey)
            background: Rectangle { radius: 10; color: groupHeader.hovered ? "#20354f" : "#192b43" }
            contentItem: Item {
                AppIcon { symbol: group.symbol; width: 22; height: 22; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                Label { text: group.title+" ("+group.count+")"; font.weight: Font.DemiBold; anchors.centerIn: parent; width: Math.max(0,parent.width-68); horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                AppIcon { symbol: "chevron"; width: 18; height: 18; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; rotation: group.expanded ? 90 : 0; Behavior on rotation { NumberAnimation { duration: 160 } } }
            }
        }
        ColumnLayout {
            id: groupBody
            objectName: group.groupKey+"Body"
            visible: group.expanded; Layout.fillWidth: true; spacing: 10
        }
        }
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
                    model: [{name:"Devices", icon:"client", page:0},{name:"Resources", icon:"settings", page:2}]
                    ToolButton {
                        id: nav
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 48; padding: 0
                        Accessible.name: modelData.name
                        onClicked: workspace.page=modelData.page
                        ToolTip.visible: hovered && workspace.collapsed; ToolTip.text: modelData.name
                        background: Rectangle { radius: 10; color: workspace.page===nav.modelData.page ? "#192e4b" : nav.hovered ? "#132136" : "transparent" }
                        contentItem: Item {
                            AppIcon { id: glyph; symbol: nav.modelData.icon; width: 22; height: 22; anchors.verticalCenter: parent.verticalCenter; x: 14*workspace.expansion+(parent.width-width)/2*(1-workspace.expansion) }
                            Label { textFormat: Text.PlainText; text: nav.modelData.name; anchors.left: glyph.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; opacity: workspace.expansion }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                ToolButton {
                    id: aboutNavigation; Layout.fillWidth: true; Layout.preferredHeight: 48; padding: 0
                    Accessible.name: "About"; onClicked: workspace.page=3
                    ToolTip.visible: hovered && workspace.collapsed; ToolTip.text: "About"
                    background: Rectangle { radius: 10; color: workspace.page===3 ? "#192e4b" : aboutNavigation.hovered ? "#132136" : "transparent" }
                    contentItem: Item {
                        AppIcon { id: aboutGlyph; symbol: "about"; width: 22; height: 22; anchors.verticalCenter: parent.verticalCenter; x: 14*workspace.expansion+(parent.width-width)/2*(1-workspace.expansion) }
                        Label { textFormat: Text.PlainText; text: "About"; anchors.left: aboutGlyph.right; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; opacity: workspace.expansion }
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
                Label { textFormat: Text.PlainText; text: ["Devices","Devices","Resources","About"][workspace.page]; font.pixelSize: 30; font.weight: Font.DemiBold }
                Label { textFormat: Text.PlainText; visible: workspace.page!==3; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aabbd3"; text: workspace.page===0 ? "Manage this device and your paired devices. Search nearby to pair a new device, then use resource power buttons to start streams." : workspace.page===1 ? "Choose what other devices can use. Sharing a resource does not activate it until someone connects." : workspace.page===2 ? "Choose resources from your devices. Each power button starts or stops only that resource." : "Displays, cameras, microphones, speakers and USB devices across your computers." }
                ColumnLayout {
                    visible: workspace.page===0; Layout.fillWidth: true; spacing: 14
                    SectionHeading { title: "This device"; Layout.topMargin: 16 }
                    Frame { Layout.fillWidth:true; padding:12
                        background: Rectangle { color:"#111f32";radius:16;border.color:"#48617e" }
                        contentItem: ColumnLayout { spacing:12
                            ToolButton { objectName:"peerLocalHeader"; Layout.fillWidth:true; padding:12; implicitHeight:64
                                onClicked:workspace.localExpanded=!workspace.localExpanded
                                background:Rectangle { color:parent.hovered?"#243a57":"#192d46";radius:12 }
                                contentItem:RowLayout { spacing:12
                                    AppIcon { symbol:"chevron";rotation:workspace.localExpanded?90:0;Layout.preferredWidth:18;Layout.preferredHeight:18 }
                                    Label { text:peers.deviceName;textFormat:Text.PlainText;font.pixelSize:18;Layout.fillWidth:true;elide:Text.ElideRight }
                                    Label { objectName:"peerLocalPairingCode"; text:peers.pairingRequired?"Pairing code: "+peers.pairingCode:"Pairing disabled";font.pixelSize:18 }
                                }
                            }
                            ColumnLayout { objectName:"peerLocalBody";Layout.fillWidth:true;visible:workspace.localExpanded;spacing:14
                    RowLayout {
                        Layout.fillWidth: true
                        Label { textFormat: Text.PlainText; text: peers.deviceName; font.pixelSize: 20; Layout.fillWidth: true }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: "Listen on" }
                        Label { visible:peers.sharing; text:"Listening: "+(peers.listeningAddress||"0.0.0.0")+":"+(peers.listeningPort||43824);color:"#9eacc2" }
                        ComboBox {
                            id: bind; objectName: "peerBindNic"; Layout.fillWidth: true
                            editable: false; textRole: "label"; valueRole: "address"
                            property string selectedAddress: currentValue==="custom" ? customBind.text.trim() : (currentValue||"0.0.0.0")
                            property string selectedPreset: "0.0.0.0"
                            model: [{label:"All addresses",address:"0.0.0.0"}].concat(peers.networkAddresses).concat([{label:"Custom address",address:"custom"}])
                            onActivated: { selectedPreset=currentValue; if(currentValue!=="custom")workspace.applyListening() }
                            onModelChanged: Qt.callLater(function(){let found=bind.indexOfValue(bind.selectedPreset);if(found<0){customBind.text=bind.selectedPreset;bind.selectedPreset="custom";found=bind.indexOfValue("custom")}bind.currentIndex=found})
                        }
                        Field { id: customBind; objectName: "peerCustomBind"; visible: bind.currentValue==="custom"; Layout.fillWidth: true; placeholderText: "Local IPv4 address"; text:"127.0.0.1" }
                        ComboBox { id: portMode; objectName:"peerPortMode"; Layout.fillWidth:true; model:["Default ? 43824","Custom port"]; onActivated:if(currentIndex===0)workspace.applyListening() }
                        Field { id: listenPort; objectName: "peerListenPort"; visible:portMode.currentIndex===1; text: "43824"; Layout.fillWidth:true; placeholderText: "Port"; validator: IntValidator { bottom:1; top:65535 } }
                        Action { objectName:"peerApplyListening"; text:"Apply"; visible:bind.currentValue==="custom"||portMode.currentIndex===1; onClicked:workspace.applyListening() }
                    }
                    Label { text: "Share makes a resource available to paired devices. The red power button stops its current streams."; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    CheckBox { text: "Require pairing"; checked: peers.pairingRequired; onToggled: peers.pairingRequired=checked }
                    Label { textFormat: Text.PlainText; visible: peers.pairingRequired; text: "The code stays the same until ExtraPane restarts. Previously paired devices use their saved identity."; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: workspace.resourceGroups
                        delegate: ResourceGroup {
                            id: offerGroup
                            required property var modelData
                            property var items: workspace.groupedRows(peers.offers, modelData)
                            title: modelData.title; symbol: modelData.icon; count: items.length
                            groupKey: "peerOffers_"+modelData.key; visible: count>0
                            Label { visible: offerGroup.modelData.key==="usb"; text: "USB forwarding is exclusive: the physical device moves to the other computer until disconnected. Use Camera or Microphone sharing to keep those devices local."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#9eacc2" }
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
                                        Switch { objectName:"peerOfferShare_"+modelData.id; text:"Share";checked:modelData.shareAllowed!==false
                                            onToggled: { peers.enableOffer(modelData.id,checked);checked=Qt.binding(function(){return modelData.shareAllowed!==false}) }
                                        }
                                        PowerButton {
                                            objectName: "peerOffer_"+modelData.id
                                            destructive: modelData.inUse===true; enabled: modelData.inUse===true
                                            description: (modelData.inUse ? "Disconnect " : modelData.shareAllowed===false ? "Not shared: " : "Available to paired devices: ")+modelData.label
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
                        }
                    }
                }
                ColumnLayout {
                    visible: workspace.page===0; Layout.fillWidth: true; spacing: 14
                    RowLayout {
                        Label { textFormat: Text.PlainText; text: "Nearby devices"; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        IconButton {
                            id: searchButton; objectName: "peerRefreshDiscovery"; symbol: "refresh"
                            implicitWidth: 44; implicitHeight: 44; padding: 10
                            description: peers.directory.scanning ? "Searching nearby devices" : "Search nearby devices"
                            onClicked: if(!peers.directory.scanning) peers.startDiscovery()
                            NumberAnimation on iconRotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: peers.directory.scanning && searchButton.visible; onStopped: searchButton.iconRotation=0 }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#2b3c51" }
                    RowLayout {
                        visible: peers.directory.scanning; spacing: 10
                        Label { text: "Searching nearby"; color: "#aabbd3" }
                        Row {
                            spacing: 6; Layout.preferredWidth: 30; Layout.preferredHeight: 18
                            Repeater {
                                model: 3
                                delegate: Item {
                                    required property int index
                                    width: 6; height: 18
                                    Rectangle {
                                        id: searchDot; width: 6; height: 6; radius: 3; y: 9; color: "#80b8ff"
                                        SequentialAnimation on y {
                                            running: peers.directory.scanning && searchDot.visible; loops: Animation.Infinite; onStopped: searchDot.y=9
                                            PauseAnimation { duration: searchDot.parent.index*200 }
                                            NumberAnimation { to: 2; duration: 100; easing.type: Easing.OutQuad }
                                            NumberAnimation { to: 9; duration: 100; easing.type: Easing.InQuad }
                                            PauseAnimation { duration: (2-searchDot.parent.index)*200 }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label { textFormat: Text.PlainText; text: peers.directory.status; visible: text.length>0 && !peers.directory.scanning; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: workspace.newNearbyDevices
                        delegate: ItemDelegate {
                            required property var modelData
                            objectName: "peerNearby_"+modelData.id
                            id: nearbyCard
                            Layout.fillWidth: true; implicitHeight: 88; padding: 18
                            Accessible.name: "Pair with "+modelData.name
                            background: Rectangle { radius: 14; color: nearbyCard.hovered ? "#20354f" : "#14243a"; border.color: nearbyCard.activeFocus ? "#80b8ff" : "#354b66" }
                            contentItem: RowLayout {
                                spacing: 16
                                AppIcon { symbol: "client"; stroke: "#9ac4ff"; Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    Label { textFormat: Text.PlainText; text: nearbyCard.modelData.name; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: nearbyCard.modelData.address+":"+nearbyCard.modelData.port+(nearbyCard.modelData.pairingRequired===false ? " ? Unverified connection" : ""); color: "#aabbd3"; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                                Label { text: "Connect"; color: "#9ac4ff" }
                                AppIcon { symbol: "chevron"; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                            }
                            onClicked: workspace.beginConnection(modelData.address,modelData.port,modelData.id,modelData.pairingRequired===false)
                        }
                    }
                    SectionHeading { title: "Paired devices"; Layout.topMargin: 16 }
                    Label { visible: peers.remembered.length===0; text: "No paired devices yet. Search nearby or connect by address to add one."; color: "#aabbd3"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Repeater {
                        model: peers.remembered
                        delegate: Frame {
                            id: pairedCard
                            required property var modelData
                            Layout.fillWidth: true; padding: 16
                            background: Rectangle { radius: 14; color: "#132237"; border.color: "#30445f" }
                            contentItem: RowLayout {
                            spacing: 12
                            AppIcon { symbol: "client"; Layout.preferredWidth: 26; Layout.preferredHeight: 26; stroke: "#9ac4ff" }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { textFormat: Text.PlainText; text: pairedCard.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { textFormat: Text.PlainText; text: pairedCard.modelData.address ? pairedCard.modelData.address+":"+pairedCard.modelData.port : "Choose this device from discovery or enter its address"; color: "#9eacc2"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                            Action { text: "Resources"; objectName: "peerPairedResources_"+pairedCard.modelData.id; visible: !pairedCard.modelData.pendingForget; onClicked: workspace.page=2 }
                            Label { text: "Removing pairing when the other device is reachable..."; visible: modelData.pendingForget===true; color: "#f0c66e"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Action { objectName: "peerClearPending_"+pairedCard.modelData.id; text: "Clear pending removal"; visible: pairedCard.modelData.pendingForget===true; onClicked: {pendingRemovalDialog.deviceId=pairedCard.modelData.id;pendingRemovalDialog.open()} }
                            Action { objectName: "peerForget_"+pairedCard.modelData.id; text: "Forget"; visible: !pairedCard.modelData.pendingForget; destructive: true; onClicked: { workspace.monitorDrafts=workspace.monitorDrafts.filter(row=>row.owner!==pairedCard.modelData.id); peers.forgetDevice(pairedCard.modelData.id) } }
                            }
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
                            Action { objectName: "peerConnect"; text: "Connect"; visible: !workspace.manualConnected; onClicked: {
                                const numeric=workspace.checkFields([port]);host.invalid=!workspace.validAddress(host.text)
                                if(!numeric||host.invalid){notification.text="Check the highlighted information.";notification.open();return}
                                workspace.beginConnection(host.text,Number(port.text),workspace.expectedDeviceId,openConnection.checked)
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
                    SectionHeading { objectName:"peerUnpairedHeading"; title: "Unpaired devices"; visible: workspace.managedDevices.length>0; Layout.topMargin: 16 }
                    Repeater {
                        model: workspace.managedDevices
                        delegate: Frame {
                            required property var modelData
                            Layout.fillWidth: true; padding: 12
                            background: Rectangle { radius: 12; color: "#111f32"; border.color: "#48617e" }
                            contentItem: RowLayout {
                                spacing: 12
                                AppIcon { symbol: "client"; Layout.preferredWidth: 26; Layout.preferredHeight: 26; stroke: "#9ac4ff" }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelData.name; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: "Identity is not verified"; color: "#f0c66e" }
                                }
                                Action { text: "Resources"; visible: !modelData.incoming; onClicked: workspace.page=2 }
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: workspace.page===2; Layout.fillWidth: true; spacing: 14
                    Label { text: "Available paired devices appear here automatically. Pair a new device on the Devices page."; visible: !peers.devices.some(row => row.incoming!==true); wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#aabbd3" }
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
                            readonly property string deviceKey: modelData.identity||String(modelData.connection)
                            readonly property bool expanded: workspace.collapsedDevices[deviceKey]===true
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
                                            Label { visible: !!peerDevice.modelData.viaConnection; text: "Via gateway - paired directly with this device"; color: "#9eacc2"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                        }
                                    }
                                }
                            }
                            ColumnLayout {
                                objectName: "peerDeviceBody_"+peerDevice.modelData.connection
                                Layout.fillWidth: true; spacing: 10; visible: peerDevice.expanded
                            Repeater {
                                model: workspace.gatewayDevices(peerDevice.modelData.resources)
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth:true
                                    Label { text: modelData.name+" (via gateway)"; textFormat:Text.PlainText; Layout.fillWidth:true; wrapMode:Text.WordWrap }
                                    Action { objectName:"peerGatewayPair_"+modelData.identity; text:"Pair device"; onClicked:workspace.beginGatewayPairing(peerDevice.modelData.connection,modelData.identity,modelData.name) }
                                }
                            }
                            Repeater {
                                model: workspace.resourceGroups
                                delegate: ResourceGroup {
                                    id: catalogGroup
                                    required property var modelData
                                    property var items: workspace.groupedRows(peerDevice.modelData.resources.filter(item=>item.delegated!==true), modelData)
                                    title: modelData.title; symbol: modelData.icon; count: modelData.key==="monitors"?workspace.monitorDrafts.filter(row=>row.connection===String(peerDevice.modelData.connection)).length:items.length
                                    groupKey: "peerCatalog_"+peerDevice.modelData.connection+"_"+modelData.key
                                    stateKey: "catalog_"+peerDevice.deviceKey+"_"+modelData.key
                                    visible: items.length>0
                                    Label { visible: catalogGroup.modelData.key==="usb"; text: "USB forwarding takes exclusive control on the other computer. Device drivers and compatibility still apply."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#9eacc2" }
                                    MonitorRows { connectionId:String(peerDevice.modelData.connection); items:catalogGroup.modelData.key==="monitors"?catalogGroup.items:[]; visible:catalogGroup.modelData.key==="monitors" }
                                    Repeater {
                                        model: catalogGroup.modelData.key==="monitors" ? [] : catalogGroup.items
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

                                                RowLayout {
                                                    visible: resourceCard.modelData.kindId===0
                                                    Field { id: modeWidth; objectName: "peerWidth_"+resourceCard.modelData.id; text: String(resourceCard.modelData.width||1920); placeholderText: "Width"; Layout.fillWidth: true; validator: IntValidator { bottom:64; top:4096 } }
                                                    Field { id: modeHeight; text: String(resourceCard.modelData.height||1080); placeholderText: "Height"; Layout.fillWidth: true; validator: IntValidator { bottom:64; top:4096 } }
                                                    Field { id: modeRefresh; text: String(Math.min(resourceCard.modelData.refresh||60,360)); placeholderText: "Hz / FPS"; Layout.preferredWidth: 90; validator: IntValidator { bottom:1; top:360 } }
                                                }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    CheckBox { id: input; visible: resourceCard.modelData.inputAllowed; checked: resourceCard.modelData.inputAllowed&&resourceCard.modelData.kindId===0; text: "Allow input" }
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
                    visible: workspace.page===3; Layout.fillWidth: true; spacing: 24
                    Frame {
                        objectName: "peerAboutCard"; Layout.fillWidth: true; padding: workspace.width<700 ? 20 : 28
                        background: Rectangle { radius: 18; color: "#14243a"; border.color: "#334b68" }
                        contentItem: ColumnLayout {
                            spacing: 20
                            RowLayout {
                                Layout.fillWidth: true; spacing: 24
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 10
                                    Label { text: "ExtraPane"; font.pixelSize: workspace.width<700 ? 24 : 32; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                    Label { text: "Your devices, working together."; color: "#c0d0e6"; font.pixelSize: 17; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                }
                                Image { objectName: "peerAboutLogo"; source: "qrc:/extrapane.png"; Layout.preferredWidth: workspace.width<700 ? 64 : 112; Layout.preferredHeight: width; fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true }
                            }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#334b68" }
                            Label { text: "Use another device as an extra monitor, or share cameras, microphones, speakers and USB devices across your network."; font.pixelSize: 16; color: "#d8e4f4"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: "Pair your devices once, then choose what to use in Resources. Devices manages your paired computers and phones, network settings and what this device makes available. Features depend on each device's operating system, hardware and permissions."; color: "#aabbd3"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { textFormat: Text.PlainText; objectName: "peerRunningVersion"; text: "Running version: "+(typeof receiver!=="undefined" ? receiver.version : Qt.application.version); color: "#9ac4ff"; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                    SectionHeading { title: "Project & support" }
                    InfoLink { title: "Documentation"; subtitle: "Setup, supported features and troubleshooting"; symbol: "book"; destination: "https://github.com/EEkebin/ExtraPane#readme" }
                    InfoLink { title: "Releases & changelog"; subtitle: "Downloads and what changed"; symbol: "refresh"; destination: "https://github.com/EEkebin/ExtraPane/releases" }
                    InfoLink { title: "GitHub repository"; subtitle: "Source code, issues and contributions"; destination: "https://github.com/EEkebin/ExtraPane" }
                    Label { text: "Third-party components retain their respective licenses. A project-wide license has not yet been selected."; color: "#91a6c0"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
                Label { textFormat: Text.PlainText; Layout.fillWidth: true; objectName:"peerStatus"; visible: workspace.page!==3 && text.length>0; text: ["Device connected","Device disconnected"].indexOf(peers.status)>=0 ? "" : peers.status; wrapMode: Text.WordWrap; color: "#aabbd3" }
            }
        }
    }
}
