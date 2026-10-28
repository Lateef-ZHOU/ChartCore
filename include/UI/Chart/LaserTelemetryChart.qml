import QtQuick
import QtQuick.Layouts

// 专用三组趋势图。QML 只装配/联动，不缓存测量值或视口，不触发设备查询。
ColumnLayout {
    id: root
    required property var configuration
    spacing: 8

    function append(at, values) {
        // 官邸提交同一时刻的完整快照；未知值用 NaN 占位，不能省略字段导致曲线行错位。
        for (const key of ["laserTemperature", "mcuTemperature", "pa1Current", "pa2Current", "pa3Current", "voltage"]) {
            const value = values[key]
            if (typeof value !== "number" || (!Number.isFinite(value) && !Number.isNaN(value))) {
                console.warn("LaserTelemetryChart requires a complete six-value snapshot")
                return false
            }
        }
        temperatures.append(at, values)
        currents.append(at, values)
        voltage.append(at, values)
        return true
    }
    function clear() {
        temperatures.clear()
        currents.clear()
        voltage.clear()
    }

    LaserTelemetryItem {
        id: temperatures
        objectName: "temperatures"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 1
        configuration: ({ quantity: LaserTelemetryItem.Temperature,
            minimum: root.configuration.temperature.minimum, maximum: root.configuration.temperature.maximum,
            windowSeconds: root.configuration.windowSeconds, capacity: root.configuration.capacity })
        onXViewChanged: function(lo, hi, follow, anchor) {
            currents.applyXView(lo, hi, follow, anchor)
            voltage.applyXView(lo, hi, follow, anchor)
        }
        onHoverMoved: function(time) { currents.applyHover(time); voltage.applyHover(time) }
        onHoverEnded: { currents.clearHover(); voltage.clearHover() }
    }
    LaserTelemetryItem {
        id: currents
        objectName: "currents"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 1
        configuration: ({ quantity: LaserTelemetryItem.Current,
            minimum: root.configuration.current.minimum, maximum: root.configuration.current.maximum,
            windowSeconds: root.configuration.windowSeconds, capacity: root.configuration.capacity })
        onXViewChanged: function(lo, hi, follow, anchor) {
            temperatures.applyXView(lo, hi, follow, anchor)
            voltage.applyXView(lo, hi, follow, anchor)
        }
        onHoverMoved: function(time) { temperatures.applyHover(time); voltage.applyHover(time) }
        onHoverEnded: { temperatures.clearHover(); voltage.clearHover() }
    }
    LaserTelemetryItem {
        id: voltage
        objectName: "voltage"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 1
        configuration: ({ quantity: LaserTelemetryItem.Voltage,
            minimum: root.configuration.voltage.minimum, maximum: root.configuration.voltage.maximum,
            windowSeconds: root.configuration.windowSeconds, capacity: root.configuration.capacity })
        onXViewChanged: function(lo, hi, follow, anchor) {
            temperatures.applyXView(lo, hi, follow, anchor)
            currents.applyXView(lo, hi, follow, anchor)
        }
        onHoverMoved: function(time) { temperatures.applyHover(time); currents.applyHover(time) }
        onHoverEnded: { temperatures.clearHover(); currents.clearHover() }
    }
}
