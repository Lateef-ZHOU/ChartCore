#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <QStringList>
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
#include "ChartTicks.h"
#include "SeriesChart.h"
#include "ChartTypes.h"

// 趋势图的通道仅在创建时配置；未指定时沿用激光器的三组图例。
class LaserTelemetryItem : public SeriesChart
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList channelKeys READ channelKeys)
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

    [[nodiscard]] const QStringList &channelKeys() const { return keys_; }
    [[nodiscard]] QVariantMap configuration() const
    {
        QVariantList channels;
        for (qsizetype i = 0; i < keys_.size(); ++i)
            channels.append(QVariantMap{{"key", keys_[i]}, {"name", series_[i].name}});
        return {{"channels", channels}, {"quantity", quantity_}, {"minimum", minimum_}, {"maximum", maximum_},
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
        keys_.clear();
        const QVariantList channels = value.value("channels", defaultChannels(quantity_)).toList();
        if (channels.isEmpty()) qFatal("Telemetry panel requires at least one channel.");
        static constexpr std::array colors{"#1e90ff", "#ff6432", "#2ea043", "#e6ad32", "#9467bd", "#17becf"};
        for (const auto &entry : channels) {
            const auto channel = entry.toMap();
            const QString key = channel.value("key").toString();
            const QString name = channel.value("name").toString();
            if (key.isEmpty() || name.isEmpty() || keys_.contains(key))
                qFatal("Telemetry channel requires a unique key and a legend name.");
            const auto colorIndex = (keys_.size() + (quantity_ == Voltage ? 4 : 0)) % colors.size();
            keys_.append(key);
            series_.append(Series{name, QColor(colors[colorIndex]), {}});
        }
        yAxis_.title = quantity_ == Temperature ? QStringLiteral("温度 (°C)")
                      : quantity_ == Current ? QStringLiteral("电流 (A)") : QStringLiteral("电压 (V)");
        yAxis_.lo = minimum;
        yAxis_.hi = maximum;
        yAxis_.minSpan = (maximum - minimum) / 1000.0;
        xAxis_.minSpan = std::min(1.0, window);
        initializeTimeWindow(window);
    }

public:
    // [主] 官邸交完整快照，本面板的所有曲线同进同删。NaN 表示尚无测量值。
    // 不在图表里缓存最新值来补齐输入；缺字段整次拒绝，不能产生不同长度的曲线。
    Q_INVOKABLE void append(const RxTime at, const QVariantMap &values)
    {
        const double seconds = std::chrono::duration<double>(at.time_since_epoch()).count();
        for (const auto &key : keys_) {
            const auto it = values.constFind(key);
            bool ok = false;
            const double value = it == values.cend() ? 0.0 : it->toDouble(&ok);
            if (!ok || std::isinf(value)) {
                qmlWarning(this) << "Incomplete or invalid telemetry snapshot:" << key;
                return;
            }
        }
        for (qsizetype i = 0; i < keys_.size(); ++i) {
            const double value = values.value(keys_[i]).toDouble();
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
        // 每行是官邸的完整快照。显示快照时刻；沿用值属于该时刻的缓存状态，不冒充各寄存器均刚回报。
        const auto &points = series_.first().points;
        qsizetype index = -1;
        for (qsizetype i = 0; i < points.size(); ++i)
            if (points[i].x() <= time && (index < 0 || points[i].x() >= points[index].x()))
                index = i;
        if (index < 0)
            return std::nullopt;
        const QString unit = quantity_ == Temperature ? QStringLiteral(" °C")
                             : quantity_ == Current ? QStringLiteral(" A") : QStringLiteral(" V");
        QString text = QStringLiteral("快照 %1").arg(clockText(points[index].x()));
        for (const auto &curve : series_) {
            const double value = curve.points[index].y();
            text += QStringLiteral("\n%1  ").arg(curve.name);
            text += std::isnan(value) ? QStringLiteral("—") : QString::number(value, 'f', 2) + unit;
        }
        return AxisText{points[index].x(), text};
    }

private:
    QStringList keys_;
    static QVariantList defaultChannels(Quantity quantity)
    {
        const auto channel = [](QString key, QString name) -> QVariant {
            return QVariantMap{{"key", key}, {"name", name}};
        };
        switch (quantity) {
        case Temperature: return {channel("laserTemperature", "激光器"), channel("mcuTemperature", "MCU")};
        case Current: return {channel("pa1Current", "PA1"), channel("pa2Current", "PA2"),
                              channel("pa3Current", "PA3"), channel("sdCurrent", "SD")};
        case Voltage: return {channel("voltage", "系统电压"), channel("pztOffset", "PZT偏移量")};
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
