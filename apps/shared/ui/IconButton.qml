import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    property string symbol: "add"
    property string description: ""
    property bool destructive: false
    property real iconRotation: 0
    implicitWidth: 36; implicitHeight: 36
    padding: 8
    Accessible.name: description
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: description
    ToolTip.delay: 500
    contentItem: AppIcon { symbol: control.symbol; rotation: control.iconRotation; stroke: !control.enabled ? "#53637b" : control.destructive ? "#ffffff" : control.down ? "#ffffff" : "#b9cbe6" }
    background: Rectangle { radius: 10; color: !control.enabled ? "transparent" : control.destructive ? (control.down ? "#8f1d2a" : control.hovered ? "#ce3949" : "#b32635") : control.down ? "#2b4161" : control.hovered || control.activeFocus ? "#20324d" : "transparent"; border.width: control.activeFocus ? 1 : 0; border.color: "#73aaff" }
}
