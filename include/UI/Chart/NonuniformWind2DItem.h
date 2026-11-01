#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <QColor>
#include <QList>
#include <QPointF>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlinfo.h>
#include <QtQml/qqmlregistration.h>
#include "UI/Chart/Base/ChartTicks.h"
#include "UI/Chart/Base/SeriesChart.h"

// 单个非均匀剖面。斜距与数值按同下标对应，只保存最新一帧，不缓存点对或时间历史。
class NonuniformWind2DItem : public SeriesChart
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration REQUIRED)
public:
    explicit NonuniformWind2DItem(QQuickItem *parent = nullptr) : SeriesChart(parent)
    {
        yAxis_.title = QStringLiteral("斜距 (m)");
        initializeInteraction(true, false);
        series_.append(Series{});
    }
    QVariantMap configuration() const
    {
        return {{"minimum", valueMin_}, {"maximum", valueMax_},
                {"rangeMinimum", rangeMin_}, {"rangeMaximum", rangeMax_},
                {"name", series_[0].name}, {"title", xAxis_.title},
                {"color", series_[0].color}, {"rightAxis", yOnRight()}};
    }
    void setConfiguration(const QVariantMap &config)
    {
        if (isComponentComplete()) {
            qmlWarning(this) << "Chart configuration is creation-only.";
            return;
        }
        const double low = config.value("minimum").toDouble(), high = config.value("maximum").toDouble();
        const double near = config.value("rangeMinimum").toDouble(), far = config.value("rangeMaximum").toDouble();
        if (!std::isfinite(low) || !std::isfinite(high) || !(high > low)
            || !std::isfinite(near) || !std::isfinite(far) || !(far > near))
            qFatal("Invalid nonuniform profile configuration.");
        valueMin_ = low; valueMax_ = high;
        rangeMin_ = near; rangeMax_ = far;
        series_[0].name = config.value("name").toString();
        series_[0].color = config.value("color").value<QColor>();
        xAxis_.title = config.value("title").toString();
        initializeInteraction(true, config.value("rightAxis").toBool());
        xAxis_.lo = low; xAxis_.hi = high;
        yAxis_.lo = near; yAxis_.hi = far;
    }

    // 入口一次检查：拒绝时完整保留上一帧。QVector 按值接纳，生产者后续修改会隐式分离。
    // 两数组皆空表示清空剖面；非有限数值表示缺测，位置必须有限且严格递增。
    Q_INVOKABLE bool setData(QVector<double> ranges, QVector<double> values)
    {
        if (ranges.size() != values.size()) return false;
        for (qsizetype i = 0; i < ranges.size(); ++i)
            if (!std::isfinite(ranges[i]) || (i > 0 && !(ranges[i] > ranges[i - 1]))) return false;
        ranges_ = std::move(ranges);
        values_ = std::move(values);
        breaks_.clear();
        for (int i = 0; i < values_.size(); ++i)
            if (!std::isfinite(values_[i]) && (i == 0 || std::isfinite(values_[i - 1])))
                breaks_.append(i);
        dataChanged();
        return true;
    }

protected:
    int drawMarks(QSGNode *clip, int index, const QRectF &plot) override
    {
        ChartThinning::thinIrregular(ranges_, yAxis_.lo, yAxis_.hi, binY(yAxis_.span()), 2, pick_);
        for (int gap : breaks_) pick_.append(gap);
        std::sort(pick_.begin(), pick_.end());
        pick_.erase(std::unique(pick_.begin(), pick_.end()), pick_.end());
        drawPolyline(clip, index, plot, series_[0].color, pick_,
                     [&](int i) { return QPointF(values_[i], ranges_[i]); });
        return 1;
    }
    const QList<Series> &series() const override { return series_; }
    std::pair<double, double> boundsX(double, double) const override { return {valueMin_, valueMax_}; }
    std::pair<double, double> boundsY(double, double) const override
    {
        if (ranges_.isEmpty()) return {rangeMin_, rangeMax_};
        if (ranges_.size() == 1) return {ranges_.first() - 0.5, ranges_.first() + 0.5};
        return {ranges_.first(), ranges_.last()};
    }
    double fullSpanX() const override { return valueMax_ - valueMin_; }
    double fullSpanY() const override { const auto [lo, hi] = boundsY(0, 0); return hi - lo; }
    QList<AxisText> ticksX(double lo, double hi, int target) const override { return ChartTicks::decimal(lo, hi, target); }
    QList<AxisText> ticksY(double lo, double hi, int target) const override { return ChartTicks::decimal(lo, hi, target); }
    std::optional<AxisText> hoverProbe(double range) const override
    {
        if (ranges_.isEmpty()) return {};
        auto it = std::lower_bound(ranges_.cbegin(), ranges_.cend(), range);
        if (it == ranges_.cend()) --it;
        else if (it != ranges_.cbegin() && range - *(it - 1) <= *it - range) --it;
        const auto index = it - ranges_.cbegin();
        return AxisText{*it, QStringLiteral("斜距  %1 m\n%2  %3")
            .arg(*it, 0, 'f', 2).arg(series_[0].name).arg(values_[index], 0, 'f', 3)};
    }
private:
    QVector<double> ranges_, values_;
    QList<int> breaks_, pick_;
    QList<Series> series_; // 仅图例元数据；points 不存数据副本。
    double valueMin_ = 0, valueMax_ = 1, rangeMin_ = 0, rangeMax_ = 1;
};
