#pragma once
#include <QVariantMap>
#include <QtQml/qqmlinfo.h>
#include <QtQml/qqml.h>
#include <QtQml/qqmlregistration.h>

#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <utility>
#include <QDateTime>
#include <QObject>
#include <QQuickItem>
#include <QString>

#include "UI/Chart/Base/ChartTicks.h"
#include "UI/Chart/Base/SeriesChart.h"
#include "UI/Chart/ChartTypes.h"
#include "Algorithm/Geometry/GimbalModel.h"

// ============================================================
// AngleChartItem.h —— 云台角度图（横轴时间 / 纵轴角度）
//
// 四条曲线共用一组时间戳：云台系水平（pan）、俯仰（tilt），世界系方位（azimuth）、高度（elevation）。
// 四条同进同删，下标始终对齐。世界角缺席（反解够不着）传 NaN，曲线在该处断开。
//
// · 纵轴固定 0～360°；横轴恒是一个设计窗宽、锚在最新采样的时刻（界住基类 QuickChartItem）。没有控件条。
// · 投喂：append(到料时刻, 四个角)，来一点收一点，**本图不限频**（铭辰的回显是 100 Hz，原样进来）。
//   画的那头靠 SeriesChart 每帧抽稀；存的那头是**定长环**（kMaxPoints），驻留量是常数。
// · 悬停：吸附到最近的采样点，竖线 + 读数框同时显示该时刻的四个值。
// · QML 直接创建绘制器、接入采样；本类独持曲线历史与手势状态。
// ============================================================

// ------------------------------------------------------------
// AngleChartItem —— 图本体（自绘 item）
// ------------------------------------------------------------
class AngleChartItem : public SeriesChart
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration REQUIRED)
public:
    explicit AngleChartItem(QQuickItem *parent = nullptr)
        : AngleChartItem(QVariantMap{{"windowSeconds", 1.0}, {"intervalSeconds", 1.0}}, parent) {}

    // C++ 与 QML 使用同一份创建配置、同一套校验和容量计算。
    explicit AngleChartItem(const QVariantMap &configuration, QQuickItem *parent = nullptr)
        : SeriesChart(parent)
    {
        static const std::array<std::pair<QString, QColor>, kCurveCount> kStyles{{
            {QStringLiteral("云台水平"), QColor(30, 144, 255)},
            {QStringLiteral("云台俯仰"), QColor(255, 100, 50)},
            {QStringLiteral("世界方位"), QColor(46, 160, 67)},
            {QStringLiteral("世界高度"), QColor(148, 103, 189)},
        }};
        for (const auto &[name, color] : kStyles)
            series_.append(Series{name, color, {}});

        xAxis_.title = QStringLiteral("时间");
        yAxis_.title = QStringLiteral("角度 (°)");
        yAxis_.lo = kAngleLo;
        yAxis_.hi = kAngleHi;
        xAxis_.minSpan = kMinTimeSpan;
        yAxis_.minSpan = kMinAngleSpan;
        configure(configuration);
    }
    QVariantMap configuration() const
    {
        return {{"windowSeconds", timeWindowSeconds()}, {"intervalSeconds", intervalSeconds_},
                {"historySeconds", historySeconds_}};
    }
    void setConfiguration(const QVariantMap &value)
    {
        if (isComponentComplete()) {
            qmlWarning(this) << "Chart configuration is creation-only.";
            return;
        }
        configure(value);
    }

    // [主] 写入一个采样（°）；横轴使用 double 秒属于本绘图器的内部表示。
    Q_INVOKABLE void append(const RxTime at, const double pan, const double tilt, const double azimuth, const double elevation)
    {
        const double tSec = std::chrono::duration<double>(at.time_since_epoch()).count();
        const std::array<double, kCurveCount> values{pan, tilt, azimuth, elevation};
        times_.append(at); // 保留原始时间精度，历史读口不从绘图的 double 横轴反算。
        for (int i = 0; i < kCurveCount; ++i)
            series_[i].points.append(QPointF(tSec, values[i]));
        noteLastX(tSec); // 横轴的锚跟着最新采样走
        // **按点数裁，不按时间裁**（用户定）：定长环，满则挤掉最旧的。
        //   · 驻留量是常数，与回显速率无关；
        //   · 不拿墙钟当裁刀——tSec 源自 RxTime（system_clock），按时间裁的话墙钟向前跳一下，
        //     整扇窗的点会被一轮删光。
        // 环比窗长一成（QuickChartItem::kRingOverWindow）：按名义回显间隔，窗的最左侧不空；回显比名义快时最左侧露空、
        // 慢时环里比窗更老的点看不到，都不是错。墙钟向后跳时新点的 x 小于旧点、序列不再单调，
        // 那是拿墙钟当横轴的固有代价，与裁剪方式无关（SeriesChart::thin 见到不单调会原样全画）。
        // 四条同进同删，删几个只按第一条算一次（Qt 6 的 QList 从头删是 O(1)）。
        if (const int over = static_cast<int>(series_[0].points.size()) - kMaxPoints; over > 0) {
            times_.remove(0, over);
            for (int i = 0; i < kCurveCount; ++i)
                series_[i].points.remove(0, over);
        }
        dataChanged();
    }

    // [主] 借出固定窗口内的世界指向值。第一项覆盖 begin，end 之后的回报不参与。
    // 不完整/乱序/无效历史返回空；相同指向合并，不在设备里另存最新角度。
    Q_INVOKABLE [[nodiscard]] WorldPoseHistory worldHistory(const RxTime begin, const RxTime end) const
    {
        if (end < begin || times_.isEmpty() || times_.first() > begin
            || !std::is_sorted(times_.cbegin(), times_.cend())) return {};
        auto first = std::upper_bound(times_.cbegin(), times_.cend(), begin);
        if (first == times_.cbegin()) return {};
        --first;
        WorldPoseHistory result;
        for (auto it = first; it != times_.cend() && *it <= end; ++it) {
            const auto index = it - times_.cbegin();
            const double azimuth = series_[2].points[index].y();
            const double elevation = series_[3].points[index].y();
            if (!std::isfinite(azimuth) || !std::isfinite(elevation)) return {};
            if (!result.empty() && result.back().second.azimuth() == azimuth
                && result.back().second.elevation() == elevation) continue;
            result.emplace_back(*it, WorldModel(azimuth, elevation));
        }
        return result;
    }

protected:
    // 横轴的界住基类（锚往前一个设计窗，外扩到整桶；本图桶长一像素一档，外扩不超过两个桶，画在轴上看不出来）。
    // 纵轴恒是 0～360°：水平 / 方位占满 0～360；俯仰与高度角只落在其中一小段，负值（铭辰俯仰可到
    // −2°）画出界、被裁掉，可接受。
    // 对照基类 `bucketed` 上方那张表，本轴是「范围恒定 + bin 恒定」那一格：范围是值域、不随实测角
    // 变；连续曲线不按桶摆图元 ⇒ bin ≡ 最小单位 ⇒ 外扩恒定 ⇒ 界恒定，交个常量就完了。
    // ⚠ 别把范围改成"随数据变"：云台停在 50° 时纵轴会缩到 50 附近。
    [[nodiscard]] std::pair<double, double> boundsY(double, double) const override
    {
        return {kAngleLo, kAngleHi};
    }
    [[nodiscard]] double fullSpanY() const override { return kAngleHi - kAngleLo; }

    // 刻度：两根轴的视口单位就是要显示的量——横轴 epoch 秒走钟表阶梯，纵轴度走十进制阶梯。
    [[nodiscard]] QList<AxisText> ticksX(const double lo, const double hi, const int target) const override
    {
        return ChartTicks::clock(lo, hi, target);
    }
    [[nodiscard]] QList<AxisText> ticksY(const double lo, const double hi, const int target) const override
    {
        return ChartTicks::decimal(lo, hi, target);
    }

    // 吸附到最近的采样点，给出该时刻的四个值
    [[nodiscard]] std::optional<AxisText> hoverProbe(const double dataX) const override
    {
        const QList<QPointF> &keys = series_[0].points;
        if (keys.isEmpty())
            return std::nullopt;
        auto it = std::lower_bound(keys.cbegin(), keys.cend(), dataX,
                                   [](const QPointF &p, const double v) { return p.x() < v; });
        if (it == keys.cend())
            --it;
        else if (it != keys.cbegin() && dataX - (it - 1)->x() < it->x() - dataX)
            --it;
        const int index = static_cast<int>(it - keys.cbegin());
        const double key = it->x();

        QString text = QDateTime::fromMSecsSinceEpoch(qRound64(key * 1000.0)).toString(QStringLiteral("HH:mm:ss.zzz"));
        for (const auto &s : series_)
        {
            const double v = s.points[index].y(); // 四条同进同删，下标对齐
            text += QStringLiteral("\n%1  %2").arg(
                s.name, std::isnan(v) ? QStringLiteral("—") : QString::number(v, 'f', 2) + QStringLiteral("°"));
        }
        return AxisText{key, text};
    }

private:
    static constexpr int kCurveCount = 4;
    // 环长按 1.1 × 历史时长 ÷ 回显间隔计算；显示窗与历史保留时长分开，由官邸一次交入。
    // 缺省历史时长等于显示窗；生产云台保留一小时，覆盖虚拟卡最长累积。
    void configure(const QVariantMap &value)
    {
        const double window = value.value("windowSeconds").toDouble();
        const double interval = value.value("intervalSeconds").toDouble();
        const double history = value.value("historySeconds", window).toDouble();
        const double capacity = std::ceil(kRingOverWindow * history / interval);
        if (!std::isfinite(window) || !std::isfinite(interval) || window <= 0 || interval <= 0
            || window < interval || !std::isfinite(history) || history < window
            || !std::isfinite(capacity) || capacity > std::numeric_limits<int>::max())
            qFatal("Invalid angle chart configuration.");
        intervalSeconds_ = interval;
        historySeconds_ = history;
        kMaxPoints = static_cast<int>(capacity);
        initializeTimeWindow(window);
    }
    const QList<Series> &series() const override { return series_; }
    QList<Series> series_;
    QList<RxTime> times_; // 与四条曲线同进同删；仅时间保留业务类型，角度直接读曲线。
    int kMaxPoints = 0;
    double intervalSeconds_ = 0;
    double historySeconds_ = 0;

    static constexpr double kAngleLo = 0.0;
    static constexpr double kAngleHi = 360.0;
    // 缩放下限：纵向 1° 已远细于云台角度精度（协议分辨率 0.01°，但读数抖动远大于此），再放大只是放大噪声。
    static constexpr double kMinAngleSpan = 1.0;
    static constexpr double kMinTimeSpan = 5.0;
};
