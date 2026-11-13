// 仅装配两个绘图器并连接联动信号，不缓存输入或维护视口副本。
import QtQuick
import Lidar.UI
import QtQuick.Layouts

RowLayout {
    id: root
    required property Wind2DItem windPlot
    required property Wind2DItem cnrPlot
    required property var specification
    readonly property var windConfiguration: ({ rows: root.specification.rows, rangeStart: root.specification.rangeStart,
            gateSpacing: root.specification.gateSpacing, minimum: root.specification.windMin, maximum: root.specification.windMax,
            name: qsTr("风速"), title: qsTr("风速 (m/s)"), color: "#1e90ff", rightAxis: false })
    readonly property var cnrConfiguration: ({ rows: root.specification.rows, rangeStart: root.specification.rangeStart,
            gateSpacing: root.specification.gateSpacing, minimum: root.specification.cnrMin, maximum: root.specification.cnrMax,
            name: "CNR", title: "CNR (dB)", color: "#ff6432", rightAxis: true })
    Component.onCompleted: {
        windPlot.objectName = "windSpeed"; windPlot.parent = windSlot; windPlot.anchors.fill = windSlot
        cnrPlot.objectName = "cnr"; cnrPlot.parent = cnrSlot; cnrPlot.anchors.fill = cnrSlot
    }
    spacing: 16
    function updateWS(values) { root.windPlot.setData(values) }
    function updateCNR(values) { root.cnrPlot.setData(values) }
    Item {
        id: windSlot
        objectName: "windSpeed"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        Connections {
            target: root.windPlot
            function onHoverMoved(coord) { root.cnrPlot.applyHover(coord) }
            function onHoverEnded() { root.cnrPlot.clearHover() }
            function onYViewChanged(lo, hi, follow) { root.cnrPlot.applyYView(lo, hi, follow) }
        }
    }
    Item {
        id: cnrSlot
        objectName: "cnr"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        Connections {
            target: root.cnrPlot
            function onHoverMoved(coord) { root.windPlot.applyHover(coord) }
            function onHoverEnded() { root.windPlot.clearHover() }
            function onYViewChanged(lo, hi, follow) { root.windPlot.applyYView(lo, hi, follow) }
        }
    }
}
