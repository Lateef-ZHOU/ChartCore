// 高度面控制页：固定配色、完整 X/Y 数据域，Z 两端相等表示全范围。
import QtQuick
import Lidar.UI
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var specification
    function updateSpectrum(values) { surface.updateSpectrum(values) }
    spacing: 4
    SurfaceChart {
        id: surface
        Layout.fillWidth: true
        Layout.fillHeight: true
        rows: root.specification.rows
        columns: root.specification.columns
        maxPower: root.specification.powerMax
    }
    FieldStrip {
        FieldStrip.Col {
            label: qsTr("功率谱范围")
            Layout.preferredWidth: 220
            RealField {
                id: lower
                Layout.fillWidth: true
                decimals: 6
                formatValue: function(v) { return v.toFixed(6).replace(/\.?0+$/, "") }
                minimum: 0; maximum: upper.value
                value: surface.valLo
                onCommitted: function(v) { surface.setRange(v, surface.valHi) }
            }
            RealField {
                id: upper
                Layout.fillWidth: true
                decimals: 6
                formatValue: function(v) { return v.toFixed(6).replace(/\.?0+$/, "") }
                minimum: lower.value; maximum: surface.maxPower
                value: surface.valHi
                onCommitted: function(v) { surface.setRange(surface.valLo, v) }
            }
        }
        FieldStrip.Col {
            label: qsTr("视角")
            Button { text: qsTr("复位"); onClicked: surface.resetCamera() }
            Label { text: qsTr("左键旋转 · 滚轮缩放") }
        }
        FieldStrip.Col {
            label: qsTr("功率谱 3D")
            Label { text: qsTr("固定配色；量程两端相等时自适应") }
        }
    }
}
