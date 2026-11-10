import QtQuick

Canvas {
    id: icon
    property string symbol: "menu"
    property color stroke: "#c9d5e8"
    implicitWidth: 24
    implicitHeight: 24
    onSymbolChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d");
        c.reset();
        c.scale(width / 24, height / 24);
        c.strokeStyle = stroke;
        c.fillStyle = stroke;
        c.lineWidth = 1.7;
        c.lineCap = "round";
        c.lineJoin = "round";
        function line(x1, y1, x2, y2) {
            c.beginPath();
            c.moveTo(x1, y1);
            c.lineTo(x2, y2);
            c.stroke();
        }
        function box(x, y, w, h) {
            c.beginPath();
            c.rect(x, y, w, h);
            c.stroke();
        }
        function circle(x, y, r) {
            c.beginPath();
            c.arc(x, y, r, 0, Math.PI * 2);
            c.stroke();
        }
        if (symbol === "server") {
            box(4, 3, 16, 7);
            box(4, 14, 16, 7);
            line(7, 6.5, 8, 6.5);
            line(7, 17.5, 8, 17.5);
            line(15, 6.5, 17, 6.5);
            line(15, 17.5, 17, 17.5);
        } else if (symbol === "client" || symbol === "display") {
            box(3, 4, 18, 13);
            line(12, 17, 12, 21);
            line(8, 21, 16, 21);
        } else if (symbol === "refresh") {
            c.beginPath();
            c.arc(12, 12, 8, 0.4, Math.PI * 1.8);
            c.stroke();
            line(19, 3, 19, 8);
            line(19, 8, 14, 8);
        } else if (symbol === "book") {
            box(5, 3, 14, 18);
            line(8, 3, 8, 21);
            line(11, 8, 16, 8);
            line(11, 12, 16, 12);
        } else if (symbol === "link") {
            line(8, 16, 16, 8);
            line(11, 5, 19, 5);
            line(19, 5, 19, 13);
            line(14, 19, 5, 19);
            line(5, 19, 5, 10);
        } else if (symbol === "settings") {
            line(4, 6, 20, 6);
            line(4, 12, 20, 12);
            line(4, 18, 20, 18);
            c.fillStyle = "#111d30";
            c.fillRect(7, 3, 4, 6);
            c.fillRect(14, 9, 4, 6);
            c.fillRect(7, 15, 4, 6);
            circle(9, 6, 2);
            circle(16, 12, 2);
            circle(9, 18, 2);
        } else if (symbol === "about") {
            circle(12, 12, 9);
            line(12, 11, 12, 17);
            circle(12, 7.5, 0.6);
        } else if (symbol === "add") {
            line(12, 5, 12, 19);
            line(5, 12, 19, 12);
        } else if (symbol === "disconnect") {
            c.beginPath();
            c.arc(12, 13, 8, -Math.PI * 0.3, Math.PI * 1.3);
            c.stroke();
            line(12, 3, 12, 12);
        } else if (symbol === "chevron") {
            c.beginPath();
            c.moveTo(9, 5);
            c.lineTo(16, 12);
            c.lineTo(9, 19);
            c.stroke();
        } else if (symbol === "camera") {
            box(3, 6, 18, 14);
            line(8, 6, 9, 3);
            line(9, 3, 15, 3);
            line(15, 3, 16, 6);
            circle(12, 13, 4);
        } else if (symbol === "speaker") {
            c.beginPath();
            c.moveTo(3, 9);
            c.lineTo(7, 9);
            c.lineTo(12, 5);
            c.lineTo(12, 19);
            c.lineTo(7, 15);
            c.lineTo(3, 15);
            c.closePath();
            c.stroke();
            c.beginPath();
            c.arc(12, 12, 6, -0.8, 0.8);
            c.stroke();
            c.beginPath();
            c.arc(12, 12, 10, -0.8, 0.8);
            c.stroke();
        } else if (symbol === "microphone") {
            c.beginPath();
            c.arc(12, 6, 3, Math.PI, 0);
            c.lineTo(15, 12);
            c.arc(12, 12, 3, 0, Math.PI);
            c.closePath();
            c.stroke();
            c.beginPath();
            c.arc(12, 12, 6, 0, Math.PI);
            c.stroke();
            line(12, 18, 12, 22);
            line(8, 22, 16, 22);
        } else if (symbol === "usb") {
            line(12, 21, 12, 3);
            line(9, 6, 12, 3);
            line(12, 3, 15, 6);
            line(12, 16, 6, 12);
            line(6, 12, 6, 8);
            circle(6, 7, 1);
            line(12, 13, 18, 10);
            line(18, 10, 18, 7);
            box(16.5, 4, 3, 3);
        } else {
            line(4, 6, 20, 6);
            line(4, 12, 20, 12);
            line(4, 18, 20, 18);
        }
    }
}
