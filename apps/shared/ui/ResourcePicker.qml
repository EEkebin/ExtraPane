pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: picker
    objectName: "resourcePicker"
    required property var resourceModel
    property var controller: null
    spacing: 10
    Label { text: "Share with the server"; font.bold: true; font.pixelSize: 16 }
    Label {
        text: "Select everything you want to use together."
        visible: picker.controller !== null
        wrapMode: Text.WordWrap; Layout.fillWidth: true
    }
    Repeater {
        model: picker.resourceModel
        Frame {
            id: card
            required property int index
            required property string resourceKey
            required property string resourceName
            required property string resourceDescription
            required property string resourceReason
            required property bool resourceAvailable
            required property bool resourceSelected
            required property var resourceDevices
            required property string resourceDeviceSelected
            required property bool resourceScanning
            required property string resourceDeviceStatus
            required property bool resourceRunning
            required property string resourceStatus
            required property bool resourceMuted
            Layout.fillWidth: true
            padding: 12
            background: Rectangle {
                color: card.resourceSelected && card.resourceAvailable ? "#1c3047" : "#172130"
                radius: 10
                border.color: card.resourceSelected && card.resourceAvailable ? "#6a9dff" : "#303d50"
            }
            ColumnLayout {
                width: parent.width
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        objectName: "resourceCheck_" + card.resourceKey
                        text: card.resourceName
                        checked: card.resourceSelected
                        enabled: !picker.resourceModel.locked && (card.resourceAvailable || card.resourceSelected)
                        Layout.preferredWidth: Math.min(140, picker.width * 0.4)
                        onClicked: picker.resourceModel.select(card.index,checked)
                    }
                    Label { text: card.resourceDescription; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
                Label {
                    visible: !card.resourceAvailable
                    text: card.resourceReason; color: "#f0c66e"
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                ColumnLayout {
                    visible: picker.controller !== null && card.resourceSelected && card.resourceAvailable && card.resourceKey !== "display"
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: sourcePicker
                            objectName: "resourceDevicePicker_" + card.resourceKey
                            Layout.fillWidth: true
                            enabled: !picker.resourceModel.locked && !card.resourceScanning && !card.resourceRunning
                            model: card.resourceDevices
                            textRole: "label"; valueRole: "id"
                            currentIndex: card.resourceDevices.findIndex(item => item.id === card.resourceDeviceSelected && item.available !== false)
                            displayText: currentIndex < 0 ? (card.resourceKey === "camera" ? "Select a camera" : card.resourceKey === "microphone" ? "Select a microphone" : "Select removable storage") : currentText
                            delegate: ItemDelegate {
                                required property int index
                                width: sourcePicker.width
                                text: card.resourceDevices[index].label
                                enabled: card.resourceDevices[index].available !== false
                                highlighted: sourcePicker.highlightedIndex === index
                            }
                            onActivated: if (picker.controller !== null) picker.controller.selectDevice(card.resourceKey,currentValue)
                        }
                        Button {
                            objectName: "resourceRefresh_" + card.resourceKey
                            text: card.resourceScanning ? "Listing..." : "Refresh"
                            enabled: !picker.resourceModel.locked && !card.resourceScanning && !card.resourceRunning
                            onClicked: if (picker.controller !== null) picker.controller.refreshDevices(card.resourceKey)
                        }
                    }
                    Label {
                        text: card.resourceDeviceStatus
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                        visible: text.length > 0
                    }
                }
                Label {
                    text: card.resourceStatus
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                    visible: picker.controller !== null && text.length > 0
                }
                RowLayout {
                    visible: picker.controller !== null && card.resourceRunning
                    Button {
                        objectName: "resourceMute_" + card.resourceKey
                        visible: card.resourceKey === "microphone"
                        text: card.resourceMuted ? "Unmute microphone" : "Mute microphone"
                        onClicked: if (picker.controller !== null) picker.controller.setMicrophoneMuted(!card.resourceMuted)
                    }
                    Button {
                        objectName: "resourceStop_" + card.resourceKey
                        text: card.resourceKey === "usb" ? "Stop USB (recovery may be required)" : "Stop " + card.resourceName.toLowerCase()
                        onClicked: if (picker.controller !== null) picker.controller.stopResource(card.resourceKey)
                    }
                }
                ColumnLayout {
                    visible: picker.controller !== null && card.resourceSelected && card.resourceKey === "usb"
                    Layout.fillWidth: true
                    Label {
                        text: "USB storage is shared exclusively. Close its files first and use Eject USB on the receiving PC before stopping. An interrupted transfer may require source recovery."
                        wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#f0c66e"
                    }
                    Button {
                        objectName: card.resourceKey === "usb" ? "usbSourceSetupButton" : ""; text: "USB source setup"
                        enabled: picker.controller !== null && !picker.controller.active && !picker.controller.hosting && !picker.controller.cameraSetupRunning
                        onClicked: picker.controller.setupUsbSource()
                    }
                    Label { text: "USB recovery"; font.bold: true }
                    Label {
                        text: picker.controller !== null ? picker.controller.recoveryStatus : ""
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    ComboBox {
                        id: recoveryPicker; objectName: card.resourceKey === "usb" ? "usbRecoveryPicker" : ""
                        Layout.fillWidth: true
                        model: picker.controller !== null ? picker.controller.usbRecoveries : []
                        textRole: "label"; valueRole: "id"
                        enabled: picker.controller !== null && !picker.controller.active && !picker.controller.hosting && !picker.controller.recoveryBusy
                    }
                    RowLayout {
                        Button {
                            objectName: card.resourceKey === "usb" ? "usbRecoveryRefreshButton" : ""; text: "Refresh recovery"
                            enabled: picker.controller !== null && !picker.controller.active && !picker.controller.hosting && !picker.controller.recoveryBusy
                            onClicked: picker.controller.refreshUsbRecovery()
                        }
                        Button {
                            objectName: card.resourceKey === "usb" ? "usbRecoveryRestoreButton" : ""; text: "Restore selected disk"
                            enabled: picker.controller !== null && !picker.controller.active && !picker.controller.hosting && !picker.controller.recoveryBusy && recoveryPicker.currentIndex >= 0 && picker.controller.usbRecoveries[recoveryPicker.currentIndex].available
                            onClicked: picker.controller.recoverUsb(recoveryPicker.currentValue)
                        }
                    }
                }
            }
        }
    }
}
