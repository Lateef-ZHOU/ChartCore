#include <QGuiApplication>
#include <cmath>
#include <limits>
#include "AngleChartItem.h"

class CreatingChart : public AngleChartItem
{
public:
    using AngleChartItem::classBegin;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const auto check = [](bool ok, const char *why) { if (!ok) qFatal("%s", why); };
    const auto time = [](int n) { return RxTime{} + std::chrono::seconds(n) + RxTime::duration(123); };
    AngleChartItem chart(QVariantMap{{"windowSeconds", 2.0}, {"intervalSeconds", 1.0}}); // 容量 3。
    check(!chart.worldHistory(time(0), time(5)), "empty angle history");
    for (int n = 0; n < 5; ++n) chart.append(time(n), n, n, n * 10, 30);
    const auto wrapped = chart.worldHistory(time(2), time(4));
    check(wrapped->size() == 3, "wrapped angle history length");
    for (std::size_t i = 0; i < wrapped->size(); ++i) {
        check((*wrapped)[i].first == time(static_cast<int>(i) + 2), "exact original timestamp");
        check((*wrapped)[i].second.azimuth() == (i + 2) * 10, "timestamp and angle stay aligned after overwrite");
    }
    const auto partial = chart.worldHistory(time(1), time(4));
    check(partial->size() == 3 && partial->front().first == time(2)
          && partial->front().second.azimuth() == 20, "partial overlap starts at first available pose");
    check(!chart.worldHistory(time(0), time(1)) && !chart.worldHistory(time(5), time(6)),
          "disjoint angle windows rejected");
    const auto instant = chart.worldHistory(time(3), time(3));
    check(instant->size() == 1 && instant->front().second.azimuth() == 30, "closed instant window");
    chart.append(time(5), 0, 0, 40, 30);
    check(chart.worldHistory(time(3), time(5))->size() == 2, "unchanged world pose coalesced");
    chart.append(time(5), 0, 0, 50, 30);
    const auto duplicate = chart.worldHistory(time(5), time(6));
    check(duplicate->size() == 1 && duplicate->front().second.azimuth() == 40, "equal-time pose does not replace accepted sample");
    chart.append(time(4), 0, 0, 60, 30);
    const auto rollback = chart.worldHistory(time(5), time(6));
    check(rollback->size() == 1 && rollback->front().second.azimuth() == 40, "clock rollback keeps accepted time and pose aligned");
    chart.append(time(6), 0, 0, 60, 30);
    chart.append(time(7), 0, 0, 70, 30);
    const auto resumed = chart.worldHistory(time(5), time(7));
    check(resumed->size() == 3 && (*resumed)[0].second.azimuth() == 40
          && (*resumed)[1].second.azimuth() == 60 && (*resumed)[2].second.azimuth() == 70,
          "history resumes above last accepted timestamp with aligned values");
    chart.append(time(8), 0, 0, std::numeric_limits<double>::quiet_NaN(), 30);
    check(!chart.worldHistory(time(6), time(8)), "invalid pose rejects the sampling window");
    check(bool(chart.worldHistory(time(6), time(7))), "later invalid sample excluded");
    CreatingChart configuring;
    configuring.classBegin();
    configuring.setConfiguration({{"windowSeconds", 3.0}, {"intervalSeconds", 1.0}});
    for (int n = 0; n < 4; ++n) configuring.append(time(n), 0, 0, n * 10, 30);
    configuring.setConfiguration({{"windowSeconds", 1.0}, {"intervalSeconds", 1.0}});
    const auto resized = configuring.worldHistory(time(2), time(3));
    check(resized->size() == 2 && resized->front().second.azimuth() == 20,
          "creation-time reconfiguration keeps time/value alignment");
    return 0;
}
