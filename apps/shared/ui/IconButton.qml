import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    property string symbol: "add"
    property string description: ""
    property real iconRotation: 0
    implicitWidth: 36; implicitHeight: 36
    padding: 8
    Accessible.name: description
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: description
    ToolTip.delay: 500
    contentItem: AppIcon { symbol: control.symbol; rotation: control.iconRotation; stroke: !control.enabled ? "#53637b" : control.down ? "#ffffff" : "#b9cbe6" }
    background: Rectangle { radius: 10; color: control.down ? "#2b4161" : control.hovered || control.activeFocus ? "#20324d" : "transparent"; border.width: control.activeFocus ? 1 : 0; border.color: "#73aaff" }
}
