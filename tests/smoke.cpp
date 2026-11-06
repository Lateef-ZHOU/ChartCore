#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDebug>
#include <SurfaceAxes.h>
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
    SurfaceChart { rows: 4; columns: 3; maxPower: 100; anchors.fill: parent }
    AngleChartItem {
        anchors.fill: parent
        configuration: ({ windowSeconds: 120, intervalSeconds: 0.01 })
    }
    Wind2DView {
        anchors.fill: parent
        specification: ({ rows: 4, gateSpacing: 60, windMin: -20, windMax: 20, cnrMin: -30, cnrMax: 40 })
    }
})QML", QUrl());
    std::unique_ptr<QObject> object(component.create());
    if (!object) { qCritical() << component.errors(); return 1; }
    const SurfaceAxes::Projection projection(SurfaceCamera{}, QSizeF(800, 600));
    const auto index = SurfaceAxes::makeScene(projection, {240, 240}, 0, 1, app.font());
    const auto distance = SurfaceAxes::makeScene(projection, {240, 240}, 0, 1, app.font(), {0, 30});
    if (index.scales[1].low != 0 || index.scales[1].high != 239
        || distance.scales[1].low != 0 || distance.scales[1].high != 7170
        || distance.scales[1].ticks.back() != 7170 || distance.scales[1].title == index.scales[1].title)
        return 2;
    return 0;
}
