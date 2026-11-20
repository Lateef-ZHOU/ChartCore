#include "DAQFamily/DAQChannelSpec.h"
#include "SurfaceItem.h"
#include "Wind2DItem.h"
#include <QEvent>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDebug>
#include <memory>
#include "AngleChartItem.h"
#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(LidarUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarChartsPlugin)
inline constexpr DAQParams<UniformRangeMapping<0.0, 60.0, 4>> TestSpec{
    .wavelengthM = 1.55e-6, .zeroWindMHz = 1,
    .frequencyStartMHz = 0, .frequencyStepMHz = 1,
    .frequencyBinCount = 3,
};
using TestSpectrum = std::shared_ptr<const Spectrum<TestSpec>>;
inline constexpr DAQParams<UniformRangeMapping<0.0, 15.0, 4>> WrongSurfaceSpec{
    .wavelengthM = 1.55e-6, .zeroWindMHz = 1,
    .frequencyStartMHz = 0, .frequencyStepMHz = 1,
    .frequencyBinCount = 3,
};
class TestSurface : public SurfaceItem {
    Q_OBJECT
public:
    explicit TestSurface(QQuickItem *parent = nullptr) : SurfaceItem(parent) {}
    Q_INVOKABLE bool setData(TestSpectrum frame, double low, double high) { return setSpectrum(frame, low, high); }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    qmlRegisterType<TestSurface>("ChartTest", 1, 0, "TestSurface");
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"QML(import QtQuick
import Lidar.Charts
import ChartTest
Item {
    width: 800; height: 600
    SurfaceChart { id: surface; plot: TestSurface { configuration: surface.plotConfiguration } rows: 4; columns: 3; rowStart: 0; rowStep: 60; columnStart: 0; columnStep: 1; maxPower: 100; anchors.fill: parent }
    property var retainedHistory
    function sampleHistory(begin, end) { return angle.worldHistory(begin, end) }
    function receiveHistory(value) { retainedHistory = value }
    function receive(values) { surface.updateSpectrum(values) }
    AngleChartItem {
        id: angle
        anchors.fill: parent
        configuration: ({ windowSeconds: 120, intervalSeconds: 0.01 })
    }
    Wind2DView {
        id: radial
        windPlot: Wind2DItem { configuration: radial.windConfiguration }
        cnrPlot: Wind2DItem { configuration: radial.cnrConfiguration }
        anchors.fill: parent
        specification: ({ rows: 4, rangeStart: 0, gateSpacing: 60, windMin: -20, windMax: 20, cnrMin: -30, cnrMax: 40 })
    }
})QML", QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object) { qCritical() << component.errors(); return 1; }
    auto *plot = object->findChild<TestSurface *>();
    if (!plot || !plot->setData(std::make_shared<const Spectrum<TestSpec>>(), 0, 100)) return 16;
    const auto revision = plot->surface().revision;
    if (plot->setSpectrum(std::make_shared<const Spectrum<WrongSurfaceSpec>>(), 0, 100)
        || plot->surface().revision != revision) return 17;
    auto frame = std::make_shared<Spectrum<TestSpec>>();
    std::fill(frame->begin(), frame->end(), 7.0);
    const double *address = &(*frame)[0];
    std::weak_ptr<const Spectrum<TestSpec>> weak = frame;
    {
        // 排队消息先保活，QML 隐藏页随后接管同一只读份额。
        const QVariant value = QVariant::fromValue(TestSpectrum(std::move(frame)));
        if (!QMetaObject::invokeMethod(object.get(), "receive", Qt::QueuedConnection, Q_ARG(QVariant, value))) return 2;
    }
    if (frame || weak.expired()) return 3;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    auto *state = object->findChild<QObject *>("surfaceState");
    if (!state) return 4;
    {
        const auto retained = state->property("raw").value<TestSpectrum>();
        if (!retained || &(*retained)[0] != address || (*retained)[5] != 7) return 5;
    }
    // 替换时旧份额由 QML/JS 释放；不留下按值帧或第二份大数组。
    const QVariant empty = QVariant::fromValue(TestSpectrum{});
    if (!QMetaObject::invokeMethod(object.get(), "receive", Q_ARG(QVariant, empty))) return 6;
    engine.collectGarbage();
    if (!weak.expired()) return 7;
    auto *angle = object->findChild<AngleChartItem *>();
    if (!angle) return 8;
    const RxTime at = std::chrono::system_clock::now();
    angle->append(at, 0, 0, 40, 20);
    QVariant sampled;
    if (!QMetaObject::invokeMethod(object.get(), "sampleHistory", Q_RETURN_ARG(QVariant, sampled),
            Q_ARG(QVariant, QVariant::fromValue(at)), Q_ARG(QVariant, QVariant::fromValue(at)))) return 15;
    auto history = sampled.value<WorldPoseHistoryPtr>();
    sampled.clear();
    if (!history || history->size() != 1) return 9;
    const auto *poseAddress = history->data();
    std::weak_ptr<const WorldPoseHistory> poseWeak = history;
    {
        const QVariant value = QVariant::fromValue(std::move(history));
        if (!QMetaObject::invokeMethod(object.get(), "receiveHistory", Qt::QueuedConnection, Q_ARG(QVariant, value))) return 10;
    }
    // 排队份额脱离源图窗仍保活，QML 取回也必须是同一窗口数组。
    delete angle;
    if (poseWeak.expired()) return 11;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    {
        const auto retained = object->property("retainedHistory").value<WorldPoseHistoryPtr>();
        if (!retained || retained->data() != poseAddress || retained->front().second.azimuth() != 40) return 12;
    }
    const QVariant noHistory = QVariant::fromValue(WorldPoseHistoryPtr{});
    if (!QMetaObject::invokeMethod(object.get(), "receiveHistory", Q_ARG(QVariant, noHistory))) return 13;
    engine.collectGarbage();
    if (!poseWeak.expired()) return 14;
    return 0;
}

#include "smoke.moc"
