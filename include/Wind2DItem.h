#pragma once
#include <memory>
#include <QVariantMap>
#include <QtQml/qqmlinfo.h>
#include <QtQml/qqml.h>
#include <QtQml/qqmlregistration.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <QColor>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QQuickItem>
#include <QString>
#include <QVector>

#include "DAQFamily/DAQChannelSpec.h"
#include "DAQChartSpecification.h"
#include "ChartThinning.h"
#include <span>
#include "ChartTicks.h"
#include "SeriesChart.h"

// ============================================================
// Wind2DItem.h —— 风速 | CNR 双剖面图
//
// 并排两块剖面面板（风速 | CNR），共用斜距轴：悬停十字线联动，纵向缩放联动。
// **没有控件条**：滚轮缩放 + 拖动平移 + 双击复位覆盖了「看哪一段」的全部需要，再设区间旋钮就要
// 维护「旋钮 ↔ 视口」两份账。3D 谱图另以数值范围控制高度钳值，不影响这里的手势视口。
//
// **剖面图的轴是横着的**：横轴 = 本帧数据值，纵轴 = 距离门（天然升序）。于是
//   · 悬停吸附在**纵轴**上、画一条**横线**（底座的 hoverOnY_）；
//   · CNR 那块的纵轴刻度画在右侧（底座的 yOnRight_），两块镜像摆放，中间不重复标斜距。
//
// **纵轴是沿波束的斜距，不是高度**：本图画的是**单束**的逐门量，束是斜的。高度 = 斜距 ×
// sin(高度角) 要知道束的朝向，而本图不认识朝向——那是风廓线的事（WindProfile）。
// 均匀通道的斜距取起点与门距，非均匀通道读取型号的静态边界；每帧数据保留通道规格。
//
// 均匀通道沿用门格视口，非均匀通道的点和视口直接使用斜距（米）。
//
// 纵向位置由创建配置确定；每帧将输入值写入绘图点列，不另存原始列表。
// ============================================================

// ============================================================
// Wind2DItem —— 单通道剖面面板（一条曲线）
// ============================================================
class Wind2DItem : public SeriesChart
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration REQUIRED)
public:
    explicit Wind2DItem(QQuickItem *parent = nullptr) : SeriesChart(parent)
    {
        yAxis_.title = QStringLiteral("斜距 (m)");
        initializeInteraction(true, false);
        series_.append(Series{});
    }
    // 创建期配置：QML 必填，组件完成后固定。
    QVariantMap configuration() const
    {
        QVariantMap result{{"rows", rowCount_},
                {"rangeStart", rangeStart_},
                {"gateSpacing", distPerBin_},
                {"minimum", valLo_},
                {"maximum", valHi_},
                {"name", series_[0].name},
                {"title", xAxis_.title},
                {"color", series_[0].color},
                {"rightAxis", yOnRight()}};
        if (!edges_.empty())
            result.insert("rangeEdges", QVariant::fromValue(StaticRangeEdges{edges_}));
        return result;
    }
    void setConfiguration(const QVariantMap &value)
    {
        if (isComponentComplete())
        {
            qmlWarning(this) << "Chart configuration is creation-only.";
            return;
        }
        const int rows = value.value("rows").toInt();
        const double start = value.value("rangeStart").toDouble();
        const double spacing = value.value("gateSpacing").toDouble();
        const auto edgeValue = value.value("rangeEdges");
        const auto edges = edgeValue.value<StaticRangeEdges>().values;
        const int low = value.value("minimum").toInt(), high = value.value("maximum").toInt();
        if (rows < 1 || high <= low)
            qFatal("Invalid wind profile configuration.");
        if (edgeValue.isValid())
        {
            if (edges.size() != static_cast<std::size_t>(rows) + 1)
                qFatal("Invalid non-uniform wind profile configuration.");
        }
        else if (!value.value("rangeStart").isValid() || !std::isfinite(start) || !std::isfinite(spacing) || spacing <= 0)
            qFatal("Invalid uniform wind profile configuration.");
        rowCount_ = rows;
        edges_ = edges;
        rangeStart_ = start;
        distPerBin_ = spacing;
        valLo_ = low;
        valHi_ = high;
        initializeInteraction(true, value.value("rightAxis").toBool());
        series_[0].name = value.value("name").toString();
        series_[0].color = value.value("color").value<QColor>();
        xAxis_.title = value.value("title").toString();
        auto &points = series_[0].points;
        points.resize(rows);
        for (int k = 0; k < rows; ++k)
            points[k] = QPointF(0, edges_.empty() ? k + 0.5 : edges_[k]);
        xAxis_.lo = low;
        xAxis_.hi = high;
        const auto [first, last] = boundsY(0, 0);
        yAxis_.lo = first;
        yAxis_.hi = last;
    }

    template<class Range>
    void setRadialWind(const std::shared_ptr<const RadialWind<Range>> &frame)
    { if (frame) setValues<Range>({frame->begin(), frame->size()}); }
    template<class Range>
    void setCNR(const std::shared_ptr<const CNR<Range>> &frame)
    { if (frame) setValues<Range>({frame->begin(), frame->size()}); }
private:
    // 只有绘制时转为借用数值；配置由 QML 创建，仍须核对其与业务规格一致。
    template<class Range>
    void setValues(std::span<const double> data)
    {
        if (data.size() != static_cast<std::size_t>(rowCount_)) return;
        if constexpr (Range::uniform)
        {
            if (!edges_.empty() || Range::start != rangeStart_ || Range::step != distPerBin_) return;
        }
        else if (edges_.data() != Range::edges.data()) return;
        QList<QPointF> &points = series_[0].points;
        breaks_.clear();
        for (int k = 0; k < rowCount_; ++k)
        {
            points[k].setX(data[k]);
            if (!std::isfinite(data[k]) && (k == 0 || std::isfinite(data[k - 1])))
                breaks_.append(k);
        }
        dataChanged(); // 只换了值；两轴的界不随这一帧数据动
    }

protected:
    // 缺测段索引在数据推送时记录一次。
    void thin(const QList<QPointF> &, const QRectF &, QList<int> &out) const override
    {
        const double bucket = binY(yAxis_.span());
        if (edges_.empty())
            ChartThinning::thinUniform(0.5, 1.0, rowCount_, yAxis_.lo, yAxis_.hi, bucket, 2, out);
        else
            ChartThinning::thinIrregular(edges_.first(rowCount_), yAxis_.lo, yAxis_.hi, bucket, 2, out);
        for (int index : breaks_) out.append(index);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }
    // 界恒定：横轴是型号宪法给的量程、纵轴是全部门格，都不随数据 / 视口 / 像素变。
    [[nodiscard]] std::pair<double, double> boundsX(double, double) const override
    {
        return {static_cast<double>(valLo_), static_cast<double>(valHi_)};
    }
    [[nodiscard]] std::pair<double, double> boundsY(double, double) const override
    {
        if (edges_.empty()) return {0.0, static_cast<double>(rowCount_)};
        return {edges_.front(), edges_.back()};
    }
    // 界与视口、像素都无关 ⇒ 「满视口下的界宽」就是界宽本身。
    [[nodiscard]] double fullSpanX() const override { return static_cast<double>(valHi_ - valLo_); }
    [[nodiscard]] double fullSpanY() const override
    { return edges_.empty() ? static_cast<double>(rowCount_) : edges_.back() - edges_.front(); }

    // 刻度。横轴的视口单位就是要显示的量（m/s 或 dB），照数写。
    [[nodiscard]] QList<AxisText> ticksX(const double lo, const double hi, const int target) const override
    {
        return ChartTicks::decimal(lo, hi, target);
    }
    // 纵轴视口走门格，字写**斜距（米）**：格心 k+½ ↦ 第 k 门的斜距 起点 + k·门距
    // ⇒ 斜距(v) = 起点 + (v − ½)·门距。
    [[nodiscard]] QList<AxisText> ticksY(const double lo, const double hi, const int target) const override
    {
        if (!edges_.empty()) return ChartTicks::decimal(lo, hi, target);
        return ChartTicks::decimal(lo, hi, target, rangeStart_ - 0.5 * distPerBin_,
                                   distPerBin_);
    }

    // 悬停：鼠标落在哪个门格就读哪个门，横线画在格心
    [[nodiscard]] std::optional<AxisText> hoverProbe(const double dataY) const override
    {
        const QList<QPointF> &points = series_[0].points;
        if (points.isEmpty())
            return std::nullopt;
        const int selected = edges_.empty() ? static_cast<int>(std::floor(dataY))
            : static_cast<int>(std::upper_bound(edges_.begin(), edges_.end(), dataY) - edges_.begin()) - 1;
        const int row = std::clamp(selected, 0, rowCount_ - 1);
        const QPointF &p = points[row];
        return AxisText{p.y(), QStringLiteral("斜距  %1 m\n%2  %3")
                                    .arg(edges_.empty() ? rangeStart_ + row * distPerBin_ : edges_[row], 0, 'f', 0)
                                    .arg(series_[0].name)
                                    .arg(p.x(), 0, 'f', 3)};
    }

private:
    const QList<Series> &series() const override { return series_; }
    QList<Series> series_;
    QList<int> breaks_; // 每次推送建立缺测段起点索引，抽稀不能跨过缺测段连线。
    int rowCount_ = 1;
    std::span<const double> edges_;
    double rangeStart_ = 0;
    double distPerBin_ = 1;
    int valLo_ = 0; // 值轴量程（型号宪法给的），界恒等于它
    int valHi_ = 1;
};
