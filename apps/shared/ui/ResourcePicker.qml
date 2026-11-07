pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: picker
    required property var resourceModel
    spacing: 8
    Label { text: "Share with the server"; font.bold: true; font.pixelSize: 16 }
    Repeater {
        model: picker.resourceModel
        Frame {
            id: card
            required property int index
            required property string resourceName
            required property string resourceDescription
            required property string resourceReason
            required property bool resourceAvailable
            required property bool resourceSelected
            Layout.fillWidth: true
            padding: 10
            background: Rectangle {
                color: card.resourceSelected && card.resourceAvailable ? "#1c3047" : "#172130"
                radius: 10
                border.color: card.resourceSelected && card.resourceAvailable ? "#6a9dff" : "#303d50"
            }
            RowLayout {
                anchors.fill: parent
                CheckBox {
                    text: card.resourceName
                    checked: card.resourceSelected
                    enabled: card.resourceAvailable && !picker.resourceModel.locked
                    Layout.preferredWidth: 140
                    onClicked: picker.resourceModel.select(card.index,checked)
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: card.resourceDescription; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label {
                        visible: !card.resourceAvailable
                        text: card.resourceReason; color: "#f0c66e"
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
