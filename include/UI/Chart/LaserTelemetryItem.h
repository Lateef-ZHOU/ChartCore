#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <QColor>
#include <QDateTime>
#include <QList>
#include <QPointF>
#include <QQuickItem>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlinfo.h>
#include <QtQml/qqmlregistration.h>
#include "UI/Chart/Base/ChartTicks.h"
#include "UI/Chart/Base/SeriesChart.h"
#include "UI/Chart/ChartTypes.h"

// 激光器趋势图的一组量纲：2 路温度 / 3 路电流 / 1 路电压。
// 暂不作为可配置任意通道的通用 Plot；不认识协议、设备或查询命令。
class LaserTelemetryItem : public SeriesChart
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration REQUIRED)
public:
    enum Quantity { Temperature, Current, Voltage };
    Q_ENUM(Quantity)

    explicit LaserTelemetryItem(QQuickItem *parent = nullptr)
        : LaserTelemetryItem({{"quantity", Temperature}, {"minimum", 0.0}, {"maximum", 100.0},
                              {"windowSeconds", 120.0}, {"capacity", 132}}, parent) {}

    explicit LaserTelemetryItem(const QVariantMap &configuration, QQuickItem *parent = nullptr) : SeriesChart(parent)
    {
        xAxis_.title = QStringLiteral("时间");
        configure(configuration);
    }

    [[nodiscard]] QVariantMap configuration() const
    {
        return {{"quantity", quantity_}, {"minimum", minimum_}, {"maximum", maximum_},
                {"windowSeconds", timeWindowSeconds()}, {"capacity", capacity_}};
    }
    void setConfiguration(const QVariantMap &value)
    {
        if (isComponentComplete()) {
            qmlWarning(this) << "Chart configuration is creation-only.";
            return;
        }
        configure(value);
    }

private:
    void configure(const QVariantMap &value)
    {
        bool validQuantity = false, validMin = false, validMax = false;
        bool validWindow = false, validCapacity = false;
        const int quantity = value.value("quantity").toInt(&validQuantity);
        const double minimum = value.value("minimum").toDouble(&validMin);
        const double maximum = value.value("maximum").toDouble(&validMax);
        const double window = value.value("windowSeconds").toDouble(&validWindow);
        const int capacity = value.value("capacity").toInt(&validCapacity);
        if (!validQuantity || quantity < Temperature || quantity > Voltage
            || !validMin || !validMax || !std::isfinite(minimum) || !std::isfinite(maximum)
            || !(maximum > minimum) || !std::isfinite(maximum - minimum)
            || !validWindow || !std::isfinite(window) || window <= 0
            || !validCapacity || capacity < 2)
            qFatal("Invalid laser telemetry chart configuration.");

        quantity_ = static_cast<Quantity>(quantity);
        minimum_ = minimum;
        maximum_ = maximum;
        capacity_ = capacity;
        series_.clear();
        for (const auto &channel : channels())
            series_.append(Series{QString::fromUtf8(channel.name), QColor(channel.color), {}});
        yAxis_.title = quantity_ == Temperature ? QStringLiteral("温度 (°C)")
                      : quantity_ == Current ? QStringLiteral("电流 (A)") : QStringLiteral("电压 (V)");
        yAxis_.lo = minimum;
        yAxis_.hi = maximum;
        yAxis_.minSpan = (maximum - minimum) / 1000.0;
        xAxis_.minSpan = std::min(1.0, window);
        initializeTimeWindow(window);
    }

public:
    // [主] 官邸只交本次收到的数值。缺字段不追加、不沿用旧值；显式 NaN 为曲线断点。
    // 每条曲线自持采样时刻，允许分包/单寄存器更新；没有凑齐六值的缓存。
    Q_INVOKABLE void append(const RxTime at, const QVariantMap &values)
    {
        const double seconds = std::chrono::duration<double>(at.time_since_epoch()).count();
        const auto group = channels();
        for (qsizetype i = 0; i < static_cast<qsizetype>(group.size()); ++i) {
            const auto it = values.constFind(QLatin1String(group[static_cast<size_t>(i)].key));
            if (it == values.cend())
                continue;
            bool ok = false;
            const double value = it->toDouble(&ok);
            if (!ok || std::isinf(value)) {
                qmlWarning(this) << "Invalid telemetry value:" << it.key();
                continue;
            }
            auto &points = series_[i].points;
            points.append(QPointF(seconds, value));
            if (points.size() > capacity_)
                points.removeFirst();
        }
        noteLastX(seconds);
        dataChanged();
    }

    // 切换被观察对象/重新开始记录由官邸显式清空；绘图不读取模块选择，也不发送查询。
    Q_INVOKABLE void clear()
    {
        for (auto &curve : series_)
            curve.points.clear();
        clearHover();
        dataChanged();
    }

protected:
    [[nodiscard]] const QList<Series> &series() const override { return series_; }
    [[nodiscard]] std::pair<double, double> boundsY(double, double) const override { return {minimum_, maximum_}; }
    [[nodiscard]] double fullSpanY() const override { return maximum_ - minimum_; }
    [[nodiscard]] QList<AxisText> ticksX(double lo, double hi, int target) const override
    { return ChartTicks::clock(lo, hi, target); }
    [[nodiscard]] QList<AxisText> ticksY(double lo, double hi, int target) const override
    { return ChartTicks::decimal(lo, hi, target); }

    [[nodiscard]] std::optional<AxisText> hoverProbe(const double time) const override
    {
        // 各曲线可以不同步。只读光标之前最近的回报，并显示它的时刻，不伪装成同一次采样。
        const QString unit = quantity_ == Temperature ? QStringLiteral(" °C")
                             : quantity_ == Current ? QStringLiteral(" A") : QStringLiteral(" V");
        QString text = clockText(time);
        bool any = false;
        for (const auto &curve : series_) {
            const QPointF *sample = nullptr;
            for (const auto &point : curve.points)
                if (point.x() <= time && (!sample || point.x() >= sample->x()))
                    sample = &point;
            text += QStringLiteral("\n%1  ").arg(curve.name);
            if (!sample) {
                text += QStringLiteral("—");
                continue;
            }
            any = true;
            text += std::isnan(sample->y()) ? QStringLiteral("—") : QString::number(sample->y(), 'f', 2) + unit;
            text += QStringLiteral("  [%1]").arg(clockText(sample->x()));
        }
        return any ? std::optional<AxisText>{{time, text}} : std::nullopt;
    }

private:
    struct Channel { const char *key; const char *name; const char *color; };
    [[nodiscard]] std::span<const Channel> channels() const
    {
        static constexpr std::array<Channel, 2> temperatures{{
            {"laserTemperature", "激光器", "#1e90ff"}, {"mcuTemperature", "MCU", "#ff6432"}}};
        static constexpr std::array<Channel, 3> currents{{
            {"pa1Current", "PA1", "#1e90ff"}, {"pa2Current", "PA2", "#ff6432"}, {"pa3Current", "PA3", "#2ea043"}}};
        static constexpr std::array<Channel, 1> voltage{{{"voltage", "系统电压", "#9467bd"}}};
        switch (quantity_) {
        case Temperature: return temperatures;
        case Current: return currents;
        case Voltage: return voltage;
        }
        Q_UNREACHABLE();
    }
    [[nodiscard]] static QString clockText(double seconds)
    { return QDateTime::fromMSecsSinceEpoch(qRound64(seconds * 1000)).toString(QStringLiteral("HH:mm:ss.zzz")); }

    Quantity quantity_ = Temperature;
    double minimum_ = 0, maximum_ = 100;
    int capacity_ = 132;
    QList<Series> series_;
};
