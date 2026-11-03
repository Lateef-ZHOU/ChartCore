// 仅装配两个绘图器并连接联动信号，不缓存输入或维护视口副本。
import QtQuick
import Lidar.UI
import QtQuick.Layouts

RowLayout {
    id: root
    required property var specification
    spacing: 16
    function updateWS(values) { wind.setData(values) }
    function updateCNR(values) { cnr.setData(values) }
    Wind2DItem {
        id: wind
        objectName: "windSpeed"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        configuration: ({ rows: root.specification.rows, gateSpacing: root.specification.gateSpacing,
            minimum: root.specification.windMin, maximum: root.specification.windMax,
            name: qsTr("风速"), title: qsTr("风速 (m/s)"), color: "#1e90ff", rightAxis: false })
        onHoverMoved: function(coord) { cnr.applyHover(coord) }
        onHoverEnded: cnr.clearHover()
        onYViewChanged: function(lo, hi, follow) { cnr.applyYView(lo, hi, follow) }
    }
    Wind2DItem {
        id: cnr
        objectName: "cnr"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        configuration: ({ rows: root.specification.rows, gateSpacing: root.specification.gateSpacing,
            minimum: root.specification.cnrMin, maximum: root.specification.cnrMax,
            name: "CNR", title: "CNR (dB)", color: "#ff6432", rightAxis: true })
        onHoverMoved: function(coord) { wind.applyHover(coord) }
        onHoverEnded: wind.clearHover()
        onYViewChanged: function(lo, hi, follow) { wind.applyYView(lo, hi, follow) }
    }
}
