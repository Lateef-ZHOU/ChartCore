import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    required property var specification
    spacing: 16
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
