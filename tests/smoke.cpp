#include "LinearArray.h"
#include <QEvent>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDebug>
#include <memory>
#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(LidarUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarChartsPlugin)
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"QML(import QtQuick
import Lidar.Charts
Item {
    width: 800; height: 600
    SurfaceChart { id: surface; rows: 4; columns: 3; rowStart: 0; rowStep: 60; columnStart: 0; columnStep: 1; maxPower: 100; anchors.fill: parent }
    function receive(values) { surface.updateSpectrum(values) }
    AngleChartItem {
        anchors.fill: parent
        configuration: ({ windowSeconds: 120, intervalSeconds: 0.01 })
    }
    Wind2DView {
        anchors.fill: parent
        specification: ({ rows: 4, rangeStart: 0, gateSpacing: 60, windMin: -20, windMax: 20, cnrMin: -30, cnrMax: 40 })
    }
})QML", QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object) { qCritical() << component.errors(); return 1; }
    auto frame = std::make_shared<LinearArray2D<double>>(4, 3, 0, 60, 0, 1, std::vector<double>(12, 7));
    const double *address = frame->data();
    std::weak_ptr<const LinearArray2D<double>> weak = frame;
    {
        // 排队消息先保活，QML 隐藏页随后接管同一只读份额。
        const QVariant value = QVariant::fromValue(LinearSpectrumPtr(std::move(frame)));
        if (!QMetaObject::invokeMethod(object.get(), "receive", Qt::QueuedConnection, Q_ARG(QVariant, value))) return 2;
    }
    if (frame || weak.expired()) return 3;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    auto *state = object->findChild<QObject *>("surfaceState");
    if (!state) return 4;
    {
        const auto retained = state->property("raw").value<LinearSpectrumPtr>();
        if (!retained || retained->data() != address || (*retained)[5] != 7) return 5;
    }
    // 替换时旧份额由 QML/JS 释放；不留下按值帧或第二份大数组。
    const QVariant empty = QVariant::fromValue(LinearSpectrumPtr{});
    if (!QMetaObject::invokeMethod(object.get(), "receive", Q_ARG(QVariant, empty))) return 6;
    engine.collectGarbage();
    if (!weak.expired()) return 7;
    return 0;
}
