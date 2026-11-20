// 原谱与请求量程只有这一份。隐藏时仅换最新输入，恢复后再准备显示数据。
import QtQuick
import Lidar.UI
import QtQuick.Window

Item {
    id: root
    objectName: "surfaceController"
    required property SurfaceItem plot
    required property int rows
    required property int columns
    // 图窗坐标在创建时交入；具体帧保留静态规格直到绘图数值边界。
    required property real rowStart
    required property real rowStep
    property var rowEdges
    required property real columnStart
    required property real columnStep
    required property real maxPower
    readonly property var plotConfiguration: ({rows: rows, columns: columns, rowStart: rowStart,
        rowStep: rowStep, columnStart: columnStart, columnStep: columnStep, rowEdges: rowEdges})
    readonly property real valLo: state.low
    readonly property real valHi: state.high
    readonly property int receivedCount: state.received
    readonly property bool presenting: visible && Window.window !== null
        && Window.window.visible && Window.window.visibility !== Window.Minimized
    QtObject {
        id: state
        objectName: "surfaceState"
        // 具体通道元组的只读共享份额在 var 中原样保存；不枚举、索引或改写它。首帧前为 undefined。
        property var raw
        property real low: 0
        property real high: 0
        property int received: 0
        property bool dirty: true
        property bool ready: false
    }
    function updateSpectrum(values) {
        state.raw = values
        state.received += 1
        state.dirty = true
        presentLatest()
    }
    function setRange(low, high) {
        if (!Number.isFinite(low) || !Number.isFinite(high)
                || low < 0 || high < low || high > maxPower)
            return false
        if (low === state.low && high === state.high)
            return true
        state.low = low
        state.high = high
        state.dirty = true
        presentLatest()
        return true
    }
    function presentLatest() {
        if (!state.ready || !presenting || !state.dirty)
            return
        if (state.raw === undefined)
            root.plot.setEmptyData(state.low, state.high)
        else
            root.plot.setData(state.raw, state.low, state.high)
        state.dirty = false
    }
    // 相机只使用现有显示数据；鼠标旋转、滚轮缩放也都由 SurfaceItem 处理。
    function resetCamera() { root.plot.resetCamera() }
    onPresentingChanged: presentLatest()
    Component.onCompleted: {
        plot.parent = root
        plot.anchors.fill = root
        state.ready = true
        presentLatest()
    }
}
