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
    void delivered(bool accepted);
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
        check(item.series()[0].points.isEmpty() && item.series()[1].points.isEmpty(), "incomplete panel snapshot rejected without partial append");
        item.append(now, {{"laserTemperature", 25.125}, {"mcuTemperature", std::numeric_limits<double>::quiet_NaN()}});
        check(item.series()[0].points[0] == QPointF(epoch, 25.125), "typed time and fractional value preserved");
        check(item.series()[0].points.size() == 1 && item.series()[1].points.size() == 1, "unknown values still occupy aligned sample row");
        check(std::isnan(item.series()[1].points.last().y()), "unknown value is a gap, not zero");
        for (int i = 1; i <= 5; ++i)
            item.append(now + std::chrono::seconds(i), {{"laserTemperature", double(i)}, {"mcuTemperature", 32.0}});
        check(item.series()[0].points.size() == 3 && item.series()[0].points.first().y() == 3, "history bounded per curve");
        check(item.series()[1].points.size() == 3 && item.series()[1].points.first().x() == item.series()[0].points.first().x(),
              "held values append and expire in aligned rows");
        item.applyHover(epoch + 5);
        check(item.hover() && item.hover()->text.contains("5.00"), "hover reads latest preceding actual sample");
        item.clear();
        check(item.series()[0].points.isEmpty() && item.series()[1].points.isEmpty() && !item.hover(), "clear drops all history and hover");
        Probe current(configuration(LaserTelemetryItem::Current));
        check(current.series().size() == 4, "current has four curves including SD");
        current.append(now, {{"pa1Current", 1.25}, {"pa2Current", 2.5}, {"pa3Current", 3.75}, {"sdCurrent", 0.03125}});
        check(current.series()[2].points.last().y() == 3.75 && current.series()[3].points.last().y() == 0.03125,
              "PA and SD current channels remain distinct with fractional amperes preserved");
        check(current.series()[3].name == "SD", "SD legend names the added current");
        Probe voltage(configuration(LaserTelemetryItem::Voltage));
        check(voltage.series().size() == 2, "voltage has system and PZT curves");
        voltage.append(now, {{"voltage", 24.1}, {"pztOffset", 2.25}});
        check(voltage.series()[0].points.last().y() == 24.1 && voltage.series()[1].points.last().y() == 2.25,
              "system voltage and PZT remain separate without scaling");
        check(voltage.series()[1].name == QStringLiteral("PZT偏移量"), "PZT legend names the added voltage");
        current.applyHover(epoch);
        voltage.applyHover(epoch);
        check(current.hover() && current.hover()->text.contains("SD  0.03 A")
            && voltage.hover() && voltage.hover()->text.contains(QStringLiteral("PZT偏移量  2.25 V")),
            "hover shows SD amperes and PZT volts");
        item.clear(); current.clear(); voltage.clear();
        const QVariantMap full{{"laserTemperature", 30.0}, {"mcuTemperature", 40.0},
            {"pa1Current", 1.0}, {"pa2Current", 2.0}, {"pa3Current", 3.0}, {"voltage", 24.0},
            {"sdCurrent", 0.03125}, {"pztOffset", 2.125}};
        for (int i = 0; i < 6; ++i) {
            const auto at = now + std::chrono::seconds(i);
            item.append(at, full); current.append(at, full); voltage.append(at, full);
            for (auto *panel : {&item, &current, &voltage})
                for (const auto &curve : panel->series())
                    check(curve.points.size() == std::min(i + 1, 3)
                          && curve.points.last().x() == epoch + i, "every full update appends eight aligned curve points");
        }
    }

    Source source;
    bool accepted = false;
    QObject::connect(&source, &Source::delivered, &app, [&](bool value) { accepted = value; });
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
            current: {minimum: 0, maximum: 5},
            voltage: {minimum: 0, maximum: 30} })
    }
    Connections {
        target: source
        function onMeasurement(at, values) { source.delivered(chart.append(at, values)) }
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
            {"pa2Current", 2 + .2 * std::cos(phase)}, {"pa3Current", 1 + .3 * std::sin(phase * 1.3)},
            {"sdCurrent", .03 + .005 * std::sin(phase)},
            {"voltage", 24 + .15 * std::sin(phase * 2)}, {"pztOffset", 2 + .1 * std::cos(phase)}});
    };
    // 只在独立演示/验证入口造数据；生产图表不拥有演示源或定时器。
    for (int i = -120; i <= 0; ++i)
        push(now + std::chrono::seconds(i), i / 12.0);
    wait(80);
    check(accepted, "QML accepts a full eight-value snapshot");
    check(sameX(temperature, current) && sameX(current, voltage), "data time ranges synchronized");
    check(temperature->yAxis().hi == 80 && current->yAxis().hi == 5 && voltage->yAxis().lo == 0,
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
    const double oldRight = temperature->xAxis().hi;
    Q_EMIT source.measurement(now + std::chrono::seconds(90), {{"voltage", 24.15}});
    check(!accepted && temperature->xAxis().hi == oldRight && sameX(temperature, voltage),
          "QML rejects incomplete snapshot before any panel advances");
    QVariantMap full{{"laserTemperature", 30.0}, {"mcuTemperature", 40.0}, {"pa1Current", 1.0},
        {"pa2Current", 2.0}, {"pa3Current", 3.0}, {"sdCurrent", 0.03}, {"voltage", 24.0}, {"pztOffset", 2.0}};
    for (const auto *key : {"sdCurrent", "pztOffset"}) {
        auto missing = full;
        missing.remove(key);
        Q_EMIT source.measurement(now + std::chrono::seconds(90), missing);
        check(!accepted && temperature->xAxis().hi == oldRight && sameX(temperature, voltage),
              "missing SD or PZT rejects all panels before appending");
    }
    wheel(temperature, Qt::ShiftModifier);
    for (int i = 0; i < 12; ++i) {
        wait(20);
        if (i == 2)
            push(now + std::chrono::milliseconds(200), 0.1);
        check(sameX(temperature, current) && sameX(current, voltage), "horizontal animation stays synchronized");
    }
    check(temperature->xAxis().span() < fullX && !temperature->xAxis().follow, "shift wheel zooms shared time");
    check(std::abs(temperature->xAxis().span() - fullX * 0.8) < 1e-5, "new snapshot does not truncate ongoing zoom");
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
    push(now + std::chrono::seconds(1), 0.2);
    wait(180);
    check(current->yAxis().span() < 10 && temperature->yAxis().span() == temperatureY
        && voltage->yAxis().span() == voltageY, "vertical zoom stays independent while new samples arrive");
    check(sameX(temperature, current) && sameX(current, voltage), "new snapshot preserves common X");

    const QPointF pos(400, 80);
    QMouseEvent reset(QEvent::MouseButtonDblClick, pos, current->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(current, &reset);
    check(current->xAxis().follow && temperature->xAxis().follow && voltage->xAxis().follow, "double click restores shared time following");
    root->findChild<QQuickItem *>("chart")->setVisible(false);
    push(now + std::chrono::seconds(20), 0.5);
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
