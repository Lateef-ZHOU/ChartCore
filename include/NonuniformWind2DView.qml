import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    required property var specification
    spacing: 16
    // 初始零值逐门显示，位置由型号提供；真实回报继续整帧替换。
    Component.onCompleted: {
        const ranges = root.specification.ranges ?? []
        const zeros = new Array(ranges.length).fill(0)
        root.updateWS(ranges, zeros)
        root.updateCNR(ranges, zeros)
    }
    function updateWS(ranges, values) { return wind.setData(ranges, values) }
    function updateCNR(ranges, values) { return cnr.setData(ranges, values) }
    NonuniformWind2DItem {
        id: wind
        objectName: "windSpeed"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        configuration: ({
            minimum: root.specification.windMin, maximum: root.specification.windMax,
            rangeMinimum: root.specification.rangeMinimum, rangeMaximum: root.specification.rangeMaximum,
            name: qsTr("风速"), title: qsTr("风速 (m/s)"), color: "#1e90ff", rightAxis: false
        })
        onHoverMoved: function(coord) { cnr.applyHover(coord) }
        onHoverEnded: cnr.clearHover()
        onYViewChanged: function(lo, hi, follow) { cnr.applyYView(lo, hi, follow) }
    }
    NonuniformWind2DItem {
        id: cnr
        objectName: "cnr"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        configuration: ({
            minimum: root.specification.cnrMin, maximum: root.specification.cnrMax,
            rangeMinimum: root.specification.rangeMinimum, rangeMaximum: root.specification.rangeMaximum,
            name: "CNR", title: "CNR (dB)", color: "#ff6432", rightAxis: true
        })
        onHoverMoved: function(coord) { wind.applyHover(coord) }
        onHoverEnded: wind.clearHover()
        onYViewChanged: function(lo, hi, follow) { wind.applyYView(lo, hi, follow) }
    }
}
