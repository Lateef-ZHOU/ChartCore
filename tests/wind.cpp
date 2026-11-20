#include "DAQFamily/DAQChannelSpec.h"
#include <span>
#include <utility>
#include <optional>
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
// 测试绘图的最小帧契约，刻意允许运行时非法网格；真实静态帧另有排队测试。
struct ChartFrameFixture {
    const std::chrono::system_clock::time_point at;
    const double start, step;
    const std::vector<std::optional<LinAlg::Vec3>> winds;
    ChartFrameFixture(std::chrono::system_clock::time_point time, double first, double spacing,
        std::vector<std::optional<LinAlg::Vec3>> data)
        : at(time), start(first), step(spacing), winds(std::move(data)) {}
    DISABLE_COPY_MOVE(ChartFrameFixture)
    auto values() const & noexcept { return std::span<const std::optional<LinAlg::Vec3>>(winds); }
    auto values() const && = delete;
    double heightStartM() const noexcept { return start; }
    double heightStepM() const noexcept { return step; }
};
using TestWindFrame = std::shared_ptr<const ChartFrameFixture>;
class TestWindBarb : public WindBarbItem<ChartFrameFixture>
{
    Q_OBJECT
public:
    explicit TestWindBarb(QQuickItem *parent = nullptr) : WindBarbItem(parent) {}
    Q_INVOKABLE void appendFrame(TestWindFrame frame) { appendColumn(std::move(frame)); }
};
class BarbProbe : public WindBarbItem<ChartFrameFixture>
{
public:
    using WindBarbItem::WindBarbItem;
    using WindBarbItem::columnCount;
    using WindBarbItem::columnValues;
    using WindBarbItem::columnStart;
    using WindBarbItem::columnStep;
    std::vector<QSGGeometry::ColoredPoint2D> geometry(const std::optional<LinAlg::Vec3> &wind)
    {
        const RxTime at(std::chrono::seconds(1000));
        appendColumn(std::make_shared<const ChartFrameFixture>(at, 50, 60, std::vector<std::optional<LinAlg::Vec3>>{wind}));
        xAxis_.lo = 990; xAxis_.hi = 1010;
        yAxis_.lo = 0; yAxis_.hi = 100;
        QSGNode root;
        drawMarks(&root, 0, plotRect());
        const auto *data = static_cast<QSGGeometryNode *>(root.firstChild())->geometry();
        const auto *vertices = data->vertexDataAsColoredPoint2D();
        return {vertices, vertices + data->vertexCount()};
    }
};
using FixedRange = UniformMapping<10.0, 15.0, 3>;
using FixedFrame = VerticalWindProfileUnit<FixedRange>;
using FixedFramePtr = std::shared_ptr<const FixedFrame>;
class FixedBarbProbe : public WindBarbItem<FixedFrame>
{
public:
    using WindBarbItem::WindBarbItem;
    using WindBarbItem::columnValues;
    using WindBarbItem::columnTime;
    using WindBarbItem::columnStart;
    using WindBarbItem::columnStep;
};
template<int Count, int Start, int Step>
using RadialRange = UniformMapping<static_cast<double>(Start), static_cast<double>(Step), Count>;
using QmlRange = RadialRange<8, 0, 15>;
using QmlWind = std::shared_ptr<const RadialWind<QmlRange>>;
using QmlCnr = std::shared_ptr<const CNR<QmlRange>>;
class TestRadialPlot : public Wind2DItem {
    Q_OBJECT
public:
    explicit TestRadialPlot(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(QmlWind value) { setRadialWind(value); }
};
class TestCnrPlot : public Wind2DItem {
    Q_OBJECT
public:
    explicit TestCnrPlot(QQuickItem *parent = nullptr) : Wind2DItem(parent) {}
    Q_INVOKABLE void setData(QmlCnr value) { setCNR(value); }
};
template<class Frame> auto filled(double value) {
    auto frame = std::make_shared<Frame>();
    std::fill(frame->begin(), frame->end(), value);
    return frame;
}
class Source : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void profile(QmlWind values);
    void cnr(QmlCnr values);
    void column(TestWindFrame frame);
    void fixedColumn(FixedFramePtr frame);
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
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> positions{0, 0.1, nan, nan, 0.2, 0.3, 100};
    ChartThinning::thinIrregular(positions, 0, 101, 1, 2, picks);
    check(picks == std::vector<int>({0, 1, 2, 4, 5, 6}), "irregular position breaks survive bucket merging");
    const std::vector<double> sparse{0, 15, 30, 3000, 3060, 12000, 12150};
    ChartThinning::thinIrregular(sparse, 0, 13000, 1000, 1, picks);
    check(picks == std::vector<int>({0, 3, 5}), "irregular samples use metres rather than array stride");
}

static QVariantMap profileConfig(double rangeStart)
{
    return {{"rows", 1000}, {"rangeStart", rangeStart}, {"gateSpacing", 60}, {"minimum", -30}, {"maximum", 30},
            {"name", "wind"}, {"title", "wind"}, {"color", QColor(Qt::blue)}};
}
static QVariantMap barbConfig()
{
    return {{"heightBins", 1}, {"windowSeconds", 2}, {"columnSeconds", 1}, {"maxHeight", 1500}};
}
static TestWindFrame frame(RxTime at, double startHeight)
{
    return std::make_shared<const ChartFrameFixture>(at, startHeight, 15.0, std::vector<std::optional<LinAlg::Vec3>>{LinAlg::Vec3{2, 1, 0}, LinAlg::Vec3{3, 2, 0}, LinAlg::Vec3{4, 3, 0}});
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
        UniformProbe item(profileConfig(0));
        item.setSize({600, 200});
        auto values = filled<RadialWind<RadialRange<1000, 0, 60>>>(1);
        (*values)[100] = nan; (*values)[101] = nan;
        item.setRadialWind<RadialRange<1000, 0, 30>>(filled<RadialWind<RadialRange<1000, 0, 30>>>(1));
        item.applyHover(100.5);
        check(item.hover()->text.contains("0.000"), "data on another grid rejected");
        item.setRadialWind<RadialRange<1000, 0, 60>>(values);
        QList<int> selected;
        item.thin({}, QRectF(0, 0, 600, 200), selected);
        check(selected.size() < 1000, "uniform profile thins along distance Y");
        check(selected.contains(100), "uniform thinning retains missing-data separator");
        item.applyHover(2.5);
        check(item.hover()->text.contains("120 m"), "gate 2 at start + 2 x spacing");
        check(item.configuration().value("rangeStart").toDouble() == 0, "range start is part of the creation configuration");
        UniformProbe shifted(profileConfig(30));
        shifted.setSize({600, 200});
        shifted.setRadialWind<RadialRange<1000, 30, 60>>(filled<RadialWind<RadialRange<1000, 30, 60>>>(1));
        shifted.applyHover(2.5);
        check(shifted.hover()->text.contains("150 m"), "explicit range start shifts every gate");
    }
    {
        BarbProbe item(barbConfig());
        item.setSize({800, 600});
        auto first = frame(now, 10);
        std::weak_ptr<const ChartFrameFixture> weak = first;
        item.appendColumn(first);
        check(item.columnCount() == 1 && item.columnValues(0).data() == first->winds.data(),
              "history shares whole immutable frame, no array copies");
        first.reset();
        item.appendColumn(std::make_shared<const ChartFrameFixture>(now, 0.0, 0.0, std::vector<std::optional<LinAlg::Vec3>>{LinAlg::Vec3{1, 2, 3}}));
        check(item.columnCount() == 1, "invalid height grid rejected before ring changes");
        for (int i = 1; i <= 4; ++i)
            item.appendColumn(frame(now + std::chrono::seconds(i), i * 100));
        check(item.columnCount() == 3 && weak.expired(), "ring capacity enforced and oldest whole frame released");
        check((item.columnStart(0) + 0 * item.columnStep(0)) == 200 && (item.columnStart(2) + 2 * item.columnStep(2)) == 430,
              "wrapped ring preserves start and step heights");
        item.appendColumn(frame(now + std::chrono::seconds(4), 900));
        item.appendColumn(frame(now + std::chrono::seconds(3), 800));
        check(item.columnCount() == 3 && (item.columnStart(0) + 0 * item.columnStep(0)) == 200
              && (item.columnStart(2) + 0 * item.columnStep(2)) == 400,
              "duplicate and older wind frames do not replace or evict accepted history");
        item.appendColumn(frame(now + std::chrono::seconds(5), 500));
        check((item.columnStart(0) + 0 * item.columnStep(0)) == 300 && (item.columnStart(2) + 0 * item.columnStep(2)) == 500,
              "wind history resumes above last accepted timestamp");
    }
    {
        const auto geometry = [](const std::optional<LinAlg::Vec3> &wind) {
            BarbProbe item(QVariantMap{{"heightBins", 1}, {"windowSeconds", 20},
                {"columnSeconds", 1}, {"maxHeight", 100}});
            item.setSize({400, 400});
            return item.geometry(wind);
        };
        const auto missing = geometry(std::nullopt), calm = geometry(LinAlg::Vec3::Zeros());
        check(missing.size() == 3 && missing[0].a == 0, "missing optional layer draws no wind glyph");
        check(calm.size() == 12 && std::all_of(calm.begin(), calm.end(), [](const auto &v) {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }), "present zero wind retains finite calm glyph");
    }
    {
        qRegisterMetaType<FixedFramePtr>();
        Source queuedSource;
        std::weak_ptr<const FixedFrame> weak;
        {
            FixedBarbProbe item(barbConfig());
            QObject::connect(&queuedSource, &Source::fixedColumn, &item,
                [&](const FixedFramePtr &frame) { item.appendColumn(frame); }, Qt::QueuedConnection);
            auto filling = std::make_shared<FixedFrame>(now, 30.0);
            (*filling)[0] = LinAlg::Vec3{0, 0, 0};
            (*filling)[2] = LinAlg::Vec3{3, 4, -1};
            const auto *data = filling->data();
            FixedFramePtr frozen = std::move(filling);
            weak = frozen;
            Q_EMIT queuedSource.fixedColumn(frozen);
            frozen.reset();
            check(!weak.expired(), "queued message keeps original static frame alive");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            check(item.columnValues(0).data() == data && item.columnTime(0) == now
                && item.columnValues(0)[0]->norm2() == 0 && !item.columnValues(0)[1]
                && item.columnValues(0)[2]->z() == -1
                && std::abs(item.columnStart(0) - 5) < 1e-9
                && std::abs(item.columnStep(0) - 7.5) < 1e-9,
                "queued static frame retains identity, missing values, timestamp and projected grid");
            for (int i = 1; i <= 3; ++i)
                item.appendColumn(std::make_shared<const FixedFrame>(now + std::chrono::seconds(i), 30));
            check(weak.expired(), "static frame released after history overwrites its last owner");
            auto last = std::make_shared<const FixedFrame>(now + std::chrono::seconds(4), 30);
            weak = last;
            item.appendColumn(last);
        }
        check(weak.expired(), "destroying typed chart releases final frame");
    }
    Source source;
    qRegisterMetaType<TestWindFrame>();
    qmlRegisterType<TestWindBarb>("WindTest", 1, 0, "TestWindBarb");
    qmlRegisterType<TestRadialPlot>("WindTest", 1, 0, "TestRadialPlot");
    qmlRegisterType<TestCnrPlot>("WindTest", 1, 0, "TestCnrPlot");
    QQmlEngine engine;
    int warnings = 0; // 类型不符的 QML 调用（如把别的帧类型交给 appendColumn）只会出警告
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const auto &list) { warnings += int(list.size()); });
    engine.rootContext()->setContextProperty("source", &source);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Lidar.Charts
import WindTest
Window {
    width: 1200; height: 800; visible: true
    color: "#202020"
    ColumnLayout {
        anchors.fill: parent
        Wind2DView {
            id: profile; objectName: "profiles"
            windPlot: TestRadialPlot { objectName: "windSpeed"; configuration: profile.windConfiguration }
            cnrPlot: TestCnrPlot { objectName: "cnr"; configuration: profile.cnrConfiguration }
            Layout.fillWidth: true; Layout.fillHeight: true
            specification: ({rows: 8, rangeStart: 0, gateSpacing: 15, windMin: -30, windMax: 30, cnrMin: -30, cnrMax: 30})
        }
        TestWindBarb {
            id: barbs; objectName: "barbs"
            Layout.fillWidth: true; Layout.fillHeight: true
            configuration: ({heightBins: 1, windowSeconds: 120, columnSeconds: 1, maxHeight: 1500})
        }
    }
    Connections {
        target: source
        function onProfile(values) { profile.updateWS(values) }
        function onCnr(values) { profile.updateCNR(values) }
        function onColumn(frame) { barbs.appendFrame(frame) }
    }
})QML", QUrl());
    std::unique_ptr<QObject> root(component.create());
    if (!root) qFatal("%s", qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *wind = root->findChild<Wind2DItem *>("windSpeed");
    auto *cnr = root->findChild<Wind2DItem *>("cnr");
    auto *barbs = root->findChild<TestWindBarb *>("barbs");
    check(window && wind && cnr && barbs, "QML registered types assemble");
    auto windValues = filled<RadialWind<QmlRange>>(0);
    auto cnrValues = filled<CNR<QmlRange>>(0);
    const std::array values{1., 3., 5., -2., 6., 12., 8., 4.};
    std::copy(values.begin(), values.end(), windValues->begin());
    std::copy(values.begin(), values.end(), cnrValues->begin());
    Q_EMIT source.profile(std::move(windValues));
    Q_EMIT source.cnr(std::move(cnrValues));
    wind->applyHover(3.5);
    check(wind->hover() && wind->hover()->text.contains("45 m") && wind->hover()->text.contains("-2.000"),
          "QML forwards values onto start + k x spacing gates");
    for (int i = 0; i < 90; i += 5) {
        std::vector<std::optional<LinAlg::Vec3>> winds;
        for (int k = 0; k < 8; ++k) winds.emplace_back(LinAlg::Vec3(3 + k / 5.0, 2 + i / 10.0, 0));
        Q_EMIT source.column(std::make_shared<const ChartFrameFixture>(now - std::chrono::seconds(90 - i), 0.0, 7.5, std::move(winds)));
    }
    check(warnings == 0, "QML forwards typed immutable whole frame without type warnings");
    wait(200);
    Q_EMIT wind->hoverMoved(2.5);
    check(cnr->hover() && cnr->hover()->text.contains("30 m"), "hover links across the pair");
    Q_EMIT wind->hoverEnded();
    check(!cnr->hover(), "hover end links");
    const double fullSpan = wind->yAxis().span();
    const QPointF pos(wind->width() / 2, wind->height() / 2);
    QWheelEvent event(pos, wind->mapToGlobal(pos), {}, QPoint(0, 120), Qt::NoButton,
                      Qt::ControlModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(wind, &event);
    wait(240);
    check(wind->yAxis().span() < fullSpan && wind->yAxis().lo == cnr->yAxis().lo
          && wind->yAxis().hi == cnr->yAxis().hi, "vertical zoom remains linked");
    window->resize(1300, 900);
    wait(80);
    if (app.arguments().contains("--snapshot")) {
        const auto image = window->grabWindow();
        check(!image.isNull() && image.save("wind.png"), "rendered snapshot saved");
    }
    qInfo() << checks << "wind chart checks passed";
    return 0;
}
#include "wind.moc"
