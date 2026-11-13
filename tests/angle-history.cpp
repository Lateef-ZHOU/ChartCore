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
    check(chart.worldHistory(time(0), time(5)).empty(), "empty angle history");
    for (int n = 0; n < 5; ++n) chart.append(time(n), n, n, n * 10, 30);
    const auto wrapped = chart.worldHistory(time(2), time(4));
    check(wrapped.size() == 3, "wrapped angle history length");
    for (std::size_t i = 0; i < wrapped.size(); ++i) {
        check(wrapped[i].first == time(static_cast<int>(i) + 2), "exact original timestamp");
        check(wrapped[i].second.azimuth() == (i + 2) * 10, "timestamp and angle stay aligned after overwrite");
    }
    check(chart.worldHistory(time(1), time(4)).empty(), "missing coverage rejected");
    const auto instant = chart.worldHistory(time(3), time(3));
    check(instant.size() == 1 && instant.front().second.azimuth() == 30, "closed instant window");
    chart.append(time(5), 0, 0, 40, 30);
    check(chart.worldHistory(time(3), time(5)).size() == 2, "unchanged world pose coalesced");
    chart.append(time(5), 0, 0, 50, 30);
    const auto duplicate = chart.worldHistory(time(5), time(6));
    check(duplicate.size() == 1 && duplicate.front().second.azimuth() == 50, "last equal-time pose anchors window");
    chart.append(time(4), 0, 0, 60, 30);
    check(chart.worldHistory(time(5), time(6)).empty(), "clock rollback rejected");
    chart.append(time(6), 0, 0, 60, 30);
    chart.append(time(7), 0, 0, 70, 30);
    check(chart.worldHistory(time(4), time(7)).size() == 2, "history recovers when rollback overwritten");
    chart.append(time(8), 0, 0, std::numeric_limits<double>::quiet_NaN(), 30);
    check(chart.worldHistory(time(6), time(8)).empty(), "invalid pose rejects the sampling window");
    check(!chart.worldHistory(time(6), time(7)).empty(), "later invalid sample excluded");
    CreatingChart configuring;
    configuring.classBegin();
    configuring.setConfiguration({{"windowSeconds", 3.0}, {"intervalSeconds", 1.0}});
    for (int n = 0; n < 4; ++n) configuring.append(time(n), 0, 0, n * 10, 30);
    configuring.setConfiguration({{"windowSeconds", 1.0}, {"intervalSeconds", 1.0}});
    const auto resized = configuring.worldHistory(time(2), time(3));
    check(resized.size() == 2 && resized.front().second.azimuth() == 20,
          "creation-time reconfiguration keeps time/value alignment");
    return 0;
}
