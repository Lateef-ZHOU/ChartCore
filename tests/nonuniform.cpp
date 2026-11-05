#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <vector>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>
#include <QtQml/QQmlExtensionPlugin>
#ifdef _WIN32
// 实物设备可先引入 Windows 头；绘图公开头也必须能在 near/far 宏存在时使用。
#include <Windows.h>
#endif
#include "NonuniformWind2DItem.h"
#include "Wind2DItem.h"
#include "WindBarbItem.h"

Q_IMPORT_QML_PLUGIN(LidarUIPlugin)
Q_IMPORT_QML_PLUGIN(LidarChartsPlugin)

static int checks = 0;
static void check(bool ok, const char *reason)
{
    ++checks;
    if (!ok) qFatal("Check failed: %s", reason);
}
static void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

class UniformProbe : public Wind2DItem
{
public:
    explicit UniformProbe(const QVariantMap &config) {
        classBegin(); setConfiguration(config); componentComplete();
    }
    using Wind2DItem::thin;
};
class ProfileProbe : public NonuniformWind2DItem
{
public:
    explicit ProfileProbe(const QVariantMap &config) {
        classBegin(); setConfiguration(config); componentComplete();
    }
};
class BarbProbe : public NonuniformWindBarbItem
{
public:
    using NonuniformWindBarbItem::NonuniformWindBarbItem;
    using NonuniformWindBarbItem::columnCount;
    using NonuniformWindBarbItem::columnAt;
};
class UniformBarbProbe : public WindBarbItem
{
public:
    using WindBarbItem::WindBarbItem;
    using WindBarbItem::columnCount;
    using WindBarbItem::columnAt;
};
class Source : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void profile(QVector<double> ranges, QVector<double> values);
    void column(NonuniformVerticalWindProfilePtr frame);
    void delivered(bool accepted);
};

static void thinning()
{
    std::mt19937 rng(8317);
    for (int trial = 0; trial < 600; ++trial) {
        const int count = int(rng() % 301);
        const double start = (int(rng() % 400) - 200) * 0.1;
        const double step = (1 + rng() % 50) * 0.1;
        const double width = std::pow(2.0, int(rng() % 12) - 5);
        const double lo = (int(rng() % 1000) - 400) * 0.1, hi = lo + width * (1 + rng() % 60);
        std::vector<double> positions;
        for (int i = 0; i < count; ++i) positions.push_back(start + i * step);
        for (int perBin : {1, 2}) {
            // 独立逐点参考：先按实际桶号分组，再取每组首末。
            std::map<double, std::vector<int>> groups;
            for (int i = 0; i < count; ++i)
                groups[std::clamp(std::floor(positions[i] / width),
                    std::floor(lo / width) - 1, std::floor(hi / width) + 1)].push_back(i);
            std::vector<int> reference, uniform, irregular;
            for (const auto &[bin, indices] : groups) {
                (void)bin;
                reference.push_back(indices.front());
                if (perBin == 2 && indices.back() != indices.front()) reference.push_back(indices.back());
            }
            ChartThinning::thinUniform(start, step, count, lo, hi, width, perBin, uniform);
            ChartThinning::thinIrregular(positions, lo, hi, width, perBin, irregular);
            check(uniform == reference, "uniform direct indices match physical buckets including negative/fractional edges");
            check(irregular == reference, "position-only irregular selection matches physical buckets");
        }
    }
    std::vector<int> picks;
    ChartThinning::thinUniform(0, 0.001, 2000000000, 100, 110, 0.125, 2, picks);
    check(picks.size() < 180 && picks.front() == 0 && picks.back() == 1999999999,
          "two billion implicit samples select only visible buckets and boundary neighbours");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> positions{0, 0.1, nan, nan, 0.2, 0.3, 100};
    ChartThinning::thinIrregular(positions, 0, 101, 1, 2, picks);
    check(picks == std::vector<int>({0, 1, 2, 4, 5, 6}), "irregular position breaks survive bucket merging");
    const std::vector<double> sparse{0, 15, 30, 3000, 3060, 12000, 12150};
    ChartThinning::thinIrregular(sparse, 0, 13000, 1000, 1, picks);
    check(picks == std::vector<int>({0, 3, 5}), "irregular samples use metres rather than array stride");
}

static QVariantMap profileConfig()
{
    return {{"minimum", -30}, {"maximum", 30}, {"rangeMinimum", 0}, {"rangeMaximum", 27000},
            {"name", "wind"}, {"title", "wind"}, {"color", QColor(Qt::blue)}};
}
static QVariantMap barbConfig()
{
    return {{"heightBins", 1}, {"windowSeconds", 2}, {"columnSeconds", 1}, {"maxHeight", 1500}};
}
static NonuniformVerticalWindProfilePtr frame(RxTime at, double height)
{
    return std::make_shared<const NonuniformVerticalWindProfileUnit>(
        at, std::vector<double>{height, height + 15, height + 150},
        std::vector<LinAlg::Vec3>{{2, 1, 0}, {3, 2, 0}, {4, 3, 0}});
}

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    thinning();
    const auto now = std::chrono::system_clock::now();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    {
        ProfileProbe item(profileConfig());
        item.setSize({600, 500});
        QVector<double> ranges{0, 15, 30, 3000, 3060, 12000};
        QVector<double> values{1, 2, 3, 4, 5, 6};
        check(item.setData(ranges, values), "accept parallel arrays");
        item.applyHover(3020);
        check(item.hover() && item.hover()->at == 3000 && item.hover()->text.contains("4.000"),
              "hover uses nearest physical distance");
        ranges[3] = 4000; values[3] = 99;
        item.applyHover(3020);
        check(item.hover()->at == 3000 && item.hover()->text.contains("4.000"), "QVector COW protects accepted snapshot");
        check(!item.setData({0, 1}, {7}), "length mismatch rejected at push");
        check(!item.setData({0, 0}, {1, 2}), "duplicate positions rejected");
        check(!item.setData({1, 0}, {1, 2}), "unsorted positions rejected");
        check(!item.setData({0, nan}, {1, 2}), "nonfinite positions rejected");
        item.applyHover(12000);
        check(item.hover()->text.contains("6.000"), "rejection leaves old full frame intact");
        check(item.setData({500}, {8}), "single sample accepted");
        check(item.yAxis().span() > 0, "single position does not collapse axis");
        item.applyHover(500);
        check(item.hover()->text.contains("8.000"), "latest profile replaces prior profile");
        check(item.setData({}, {}), "empty parallel arrays clear profile");
        item.applyHover(0);
        check(!item.hover(), "empty profile has no old values");
    }
    {
        auto config = profileConfig();
        config.insert("rows", 1000); config.insert("gateSpacing", 60);
        UniformProbe item(config);
        item.setSize({600, 200});
        QVector<double> values(1000, 1);
        values[100] = nan; values[101] = nan;
        item.setData(values);
        QList<int> selected;
        item.thin({}, QRectF(0, 0, 600, 200), selected);
        check(selected.size() < 1000, "uniform profile now thins along distance Y");
        check(selected.contains(100), "uniform thinning retains missing-data separator");
        item.applyHover(2.5);
        check(item.hover()->text.contains("180 m"), "uniform legacy gate convention unchanged");
    }
    {
        BarbProbe item(barbConfig());
        item.setSize({800, 600});
        auto first = frame(now, 10);
        std::weak_ptr<const NonuniformVerticalWindProfileUnit> weak = first;
        check(item.appendColumn(first), "accept nonuniform whole column");
        check(item.columnAt(0).winds.data() == first->winds.data()
              && item.columnAt(0).heights.data() == first->heights.data(), "history shares whole immutable frame, no array copies");
        first.reset();
        auto bad = std::make_shared<const NonuniformVerticalWindProfileUnit>(
            now, std::vector<double>{1}, std::vector<LinAlg::Vec3>{});
        check(!item.appendColumn(bad) && item.columnCount() == 1, "mismatched column rejected before ring changes");
        auto duplicate = std::make_shared<const NonuniformVerticalWindProfileUnit>(
            now, std::vector<double>{1, 1}, std::vector<LinAlg::Vec3>{{1, 2, 3}, {1, 2, 3}});
        check(!item.appendColumn(duplicate), "nonuniform height order checked at ingress");
        for (int i = 1; i <= 4; ++i)
            check(item.appendColumn(frame(now + std::chrono::seconds(i), i * 100)), "append whole frame");
        check(item.columnCount() == 3 && weak.expired(), "ring capacity enforced and oldest whole frame released");
        check(item.columnAt(0).heightAt(0) == 200 && item.columnAt(2).heightAt(2) == 550,
              "wrapped ring preserves matching heights and winds");
        UniformBarbProbe uniform(barbConfig());
        uniform.setSize({800, 600});
        auto regular = std::make_shared<const VerticalWindProfileUnit>(
            now, 10.0, 15.0, std::vector<LinAlg::Vec3>{{1, 0, 0}, {2, 0, 0}});
        uniform.appendColumn(regular);
        check(uniform.columnCount() == 1 && uniform.columnAt(0).heightAt(1) == 25,
              "uniform API and frame sharing retained");
    }
    Source source;
    qRegisterMetaType<NonuniformVerticalWindProfilePtr>();
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("source", &source);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Lidar.Charts
Window {
    width: 1200; height: 800; visible: true
    color: "#202020"
    ColumnLayout {
        anchors.fill: parent
        NonuniformWind2DView {
            id: profile; objectName: "profiles"
            Layout.fillWidth: true; Layout.fillHeight: true
            specification: ({windMin: -30, windMax: 30, cnrMin: -30, cnrMax: 30,
                             rangeMinimum: 0, rangeMaximum: 1500, ranges: [0, 15, 30, 300, 1500]})
        }
        NonuniformWindBarbItem {
            id: barbs; objectName: "barbs"
            Layout.fillWidth: true; Layout.fillHeight: true
            configuration: ({heightBins: 1, windowSeconds: 120, columnSeconds: 1, maxHeight: 1500})
        }
    }
    Connections {
        target: source
        function onProfile(ranges, values) { source.delivered(profile.updateWS(ranges, values)); profile.updateCNR(ranges, values) }
        function onColumn(frame) { source.delivered(barbs.appendColumn(frame)) }
    }
})QML", QUrl());
    std::unique_ptr<QObject> root(component.create());
    if (!root) qFatal("%s", qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *wind = root->findChild<NonuniformWind2DItem *>("windSpeed");
    auto *cnr = root->findChild<NonuniformWind2DItem *>("cnr");
    auto *barbs = root->findChild<NonuniformWindBarbItem *>("barbs");
    check(window && wind && cnr && barbs, "QML registered types assemble");
    for (auto *profile : {wind, cnr}) {
        profile->applyHover(28);
        check(profile->hover() && profile->hover()->at == 30
              && profile->hover()->text.contains("0.000"), "initial hover snaps to interior gate");
    }
    bool accepted = false;
    QObject::connect(&source, &Source::delivered, &source, [&](bool ok) { accepted = ok; });
    Q_EMIT source.profile({0, 15, 30, 150, 300, 600, 900, 1500}, {1, 3, 5, -2, 6, 12, 8, 4});
    check(accepted, "QML forwards separate profile arrays");
    for (int i = 0; i < 90; i += 5) {
        auto data = std::make_shared<NonuniformVerticalWindProfileUnit>();
        data->at = now - std::chrono::seconds(90 - i);
        data->heights = {0, 15, 30, 150, 300, 600, 900, 1500};
        for (double h : data->heights) data->winds.emplace_back(3 + h / 500, 2 + i / 10.0, 0);
        Q_EMIT source.column(std::move(data));
    }
    check(accepted, "QML forwards typed immutable whole frame");
    wait(200);
    Q_EMIT wind->hoverMoved(601);
    check(cnr->hover() && cnr->hover()->at == 600, "hover links in physical metres");
    Q_EMIT wind->hoverEnded();
    check(!cnr->hover(), "hover end links");
    const double fullSpan = wind->yAxis().span();
    const QPointF pos(wind->width() / 2, wind->height() / 2);
    QWheelEvent event(pos, wind->mapToGlobal(pos), {}, QPoint(0, 120), Qt::NoButton,
                      Qt::ControlModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(wind, &event);
    wait(240);
    check(wind->yAxis().span() < fullSpan && wind->yAxis().lo == cnr->yAxis().lo
          && wind->yAxis().hi == cnr->yAxis().hi, "nonuniform vertical zoom remains linked");
    barbs->setVisible(false);
    Q_EMIT source.column(frame(now, 300));
    check(accepted, "hidden wind barbs still retain frame");
    barbs->setVisible(true);
    window->resize(1300, 900);
    wait(80);
    if (app.arguments().contains("--snapshot")) {
        const auto image = window->grabWindow();
        check(!image.isNull() && image.save("nonuniform.png"), "rendered snapshot saved");
    }
    wait(8000);
    qInfo() << checks << "nonuniform chart checks passed";
    return 0;
}
#include "nonuniform.moc"
