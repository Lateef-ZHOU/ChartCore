#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>
#include <QtQml/QQmlExtensionPlugin>
#include "UI/Chart/LaserTelemetryItem.h"

Q_IMPORT_QML_PLUGIN(LidarUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarChartsPlugin)

class Source : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void measurement(RxTime at, const QVariantMap &values);
};

class Probe : public LaserTelemetryItem
{
public:
    using LaserTelemetryItem::LaserTelemetryItem;
    using LaserTelemetryItem::series;
};

static int checks = 0;
static void check(bool ok, const char *reason)
{
    ++checks;
    if (!ok) qFatal("Check failed: %s", reason);
}
static void wait(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
static QVariantMap configuration(int quantity, int capacity = 3)
{
    return {{"quantity", quantity}, {"minimum", -10}, {"maximum", 90},
            {"windowSeconds", 120}, {"capacity", capacity}};
}
static void wheel(QQuickItem *item, Qt::KeyboardModifiers modifiers)
{
    const auto pos = QPointF(item->width() / 2, item->height() / 2);
    QWheelEvent event(pos, item->mapToGlobal(pos), {}, QPoint(0, 120), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(item, &event);
}
static bool sameX(const QuickChartItem *a, const QuickChartItem *b)
{
    return a->xAxis().lo == b->xAxis().lo && a->xAxis().hi == b->xAxis().hi
        && a->xAxis().follow == b->xAxis().follow;
}

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    qRegisterMetaType<RxTime>();
    const auto now = std::chrono::system_clock::now();
    const double epoch = std::chrono::duration<double>(now.time_since_epoch()).count();
    {
        Probe item(configuration(LaserTelemetryItem::Temperature));
        item.setSize({800, 240});
        check(item.series().size() == 2, "temperature has two curves");
        item.append(now, {{"laserTemperature", 25.125}});
        check(item.series()[0].points.size() == 1 && item.series()[1].points.isEmpty(), "missing is not zero or old value");
        check(item.series()[0].points[0] == QPointF(epoch, 25.125), "typed time and fractional value preserved");
        item.append(now, {{"mcuTemperature", 32.0}});
        check(item.series()[0].points.size() == 1 && item.series()[1].points.size() == 1, "split readings independent");
        item.append(now, {{"mcuTemperature", std::numeric_limits<double>::quiet_NaN()}});
        check(std::isnan(item.series()[1].points.last().y()), "explicit gap preserved");
        for (int i = 1; i <= 5; ++i)
            item.append(now + std::chrono::seconds(i), {{"laserTemperature", double(i)}});
        check(item.series()[0].points.size() == 3 && item.series()[0].points.first().y() == 3, "history bounded per curve");
        check(item.series()[1].points.size() == 2, "other channel history not fabricated");
        item.applyHover(epoch + 5);
        check(item.hover() && item.hover()->text.contains("5.00"), "hover reads latest preceding actual sample");
        item.clear();
        check(item.series()[0].points.isEmpty() && item.series()[1].points.isEmpty() && !item.hover(), "clear drops all history and hover");
        Probe current(configuration(LaserTelemetryItem::Current));
        check(current.series().size() == 3, "current has three curves");
        current.append(now, {{"pa1Current", 1.25}, {"pa2Current", 2.5}, {"pa3Current", 3.75}});
        check(current.series()[2].points.last().y() == 3.75, "current channels remain distinct");
        Probe voltage(configuration(LaserTelemetryItem::Voltage));
        check(voltage.series().size() == 1, "voltage has one curve");
        voltage.append(now, {{"voltage", 24.1}});
        check(voltage.series()[0].points.last().y() == 24.1, "voltage value delivered");
    }

    Source source;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("source", &source);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import Lidar.Charts
Window {
    width: 1050; height: 900; visible: true
    title: "激光器趋势图 · 演示数据"
    color: palette.base
    LaserTelemetryChart {
        id: chart
        objectName: "chart"
        anchors.fill: parent; anchors.margins: 16
        configuration: ({ windowSeconds: 120, capacity: 132,
            temperature: {minimum: 0, maximum: 80},
            current: {minimum: 0, maximum: 10},
            voltage: {minimum: 20, maximum: 30} })
    }
    Connections {
        target: source
        function onMeasurement(at, values) { chart.append(at, values) }
    }
})QML", QUrl());
    std::unique_ptr<QObject> root(component.create());
    if (!root) { qCritical() << component.errors(); return 1; }
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *temperature = root->findChild<LaserTelemetryItem *>("temperatures");
    auto *current = root->findChild<LaserTelemetryItem *>("currents");
    auto *voltage = root->findChild<LaserTelemetryItem *>("voltage");
    check(window && temperature && current && voltage, "QML builds all three plots");

    auto push = [&](RxTime at, double phase) {
        Q_EMIT source.measurement(at, {{"laserTemperature", 36 + 2 * std::sin(phase)},
            {"mcuTemperature", 43 + std::cos(phase * .7)}, {"pa1Current", 3 + .4 * std::sin(phase)},
            {"pa2Current", 5 + .2 * std::cos(phase)}, {"pa3Current", 7 + .3 * std::sin(phase * 1.3)},
            {"voltage", 24 + .15 * std::sin(phase * 2)}});
    };
    // 只在独立演示/验证入口造数据；生产图表不拥有演示源或定时器。
    for (int i = -120; i <= 0; ++i)
        push(now + std::chrono::seconds(i), i / 12.0);
    wait(80);
    check(sameX(temperature, current) && sameX(current, voltage), "data time ranges synchronized");
    check(temperature->yAxis().hi == 80 && current->yAxis().hi == 10 && voltage->yAxis().lo == 20,
          "three independent unit ranges");
    check(temperature->width() == current->width() && temperature->height() == current->height(), "vertical layout aligned");

    const bool preview = app.arguments().contains("--preview");
    const int captureIndex = app.arguments().indexOf("--capture");
    if (preview || captureIndex >= 0) {
        if (captureIndex >= 0 && captureIndex + 1 < app.arguments().size()) {
            wait(300);
            check(window->grabWindow().save(app.arguments()[captureIndex + 1]), "preview image saved");
            return 0;
        }
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &source, [&] {
            const auto time = std::chrono::system_clock::now();
            push(time, std::chrono::duration<double>(time - now).count() / 12.0);
        });
        timer.start(1000);
        return app.exec();
    }

    const double fullX = temperature->xAxis().span();
    wheel(temperature, Qt::ShiftModifier);
    for (int i = 0; i < 12; ++i) {
        wait(20);
        if (i == 2)
            Q_EMIT source.measurement(now + std::chrono::milliseconds(200), {{"voltage", 24.15}});
        check(sameX(temperature, current) && sameX(current, voltage), "horizontal animation stays synchronized");
    }
    check(temperature->xAxis().span() < fullX && !temperature->xAxis().follow, "shift wheel zooms shared time");
    check(std::abs(temperature->xAxis().span() - fullX * 0.8) < 1e-5, "partial data does not truncate ongoing zoom");
    const double beforeDrag = voltage->xAxis().lo;
    const QPointF from(420, 100), to(460, 100);
    QMouseEvent press(QEvent::MouseButtonPress, from, voltage->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, to, voltage->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, to, voltage->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(voltage, &press);
    QCoreApplication::sendEvent(voltage, &move);
    QCoreApplication::sendEvent(voltage, &release);
    check(voltage->xAxis().lo != beforeDrag && sameX(temperature, voltage) && sameX(current, voltage), "drag pans all time axes");
    const double temperatureY = temperature->yAxis().span();
    const double voltageY = voltage->yAxis().span();
    wheel(current, Qt::ControlModifier);
    wait(60);
    Q_EMIT source.measurement(now + std::chrono::seconds(1), {{"voltage", 24.2}});
    wait(180);
    check(current->yAxis().span() < 10 && temperature->yAxis().span() == temperatureY
        && voltage->yAxis().span() == voltageY, "vertical zoom stays independent while new samples arrive");
    check(sameX(temperature, current) && sameX(current, voltage), "partial reply preserves common X");

    const QPointF pos(400, 80);
    QMouseEvent reset(QEvent::MouseButtonDblClick, pos, current->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(current, &reset);
    check(current->xAxis().follow && temperature->xAxis().follow && voltage->xAxis().follow, "double click restores shared time following");
    root->findChild<QQuickItem *>("chart")->setVisible(false);
    Q_EMIT source.measurement(now + std::chrono::seconds(20), {{"voltage", 24.5}});
    window->resize(1100, 960);
    wait(40);
    root->findChild<QQuickItem *>("chart")->setVisible(true);
    wait(40);
    check(sameX(temperature, current) && sameX(current, voltage), "hidden update and resize keep common time");
    check(temperature->xAxis().hi >= epoch + 20, "hidden updates retained");
    Q_EMIT current->hoverMoved(epoch);
    check(temperature->hover() && voltage->hover(), "hover linked across units");
    Q_EMIT current->hoverEnded();
    check(!temperature->hover() && !voltage->hover(), "hover cleared across units");
    wait(8000);
    qInfo() << checks << "laser telemetry checks passed";
    return 0;
}
#include "laser-telemetry.moc"
