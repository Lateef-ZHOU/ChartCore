#pragma once
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

#include "Algorithm/Wind/RangeGate.h" // 门下标 → 斜距（与风廓线共用的唯一一处换算）
#include "UI/Chart/Base/ChartTicks.h"
#include "UI/Chart/Base/SeriesChart.h"

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
// 门 → 斜距的口径只认 RangeGate::rangeOf 那一处（与风廓线共用）。
//
// **纵轴的视口单位是门格**：第 k 门占 [k, k+1)，折线过格心 k+½；米只出现在刻度文字与悬停读数里
// （见 ticksY / hoverProbe）。这只对斜距成立：门 → 斜距不随束的朝向变，所以每一帧都落在同一套
// 门格上。风羽图的纵轴是高度，每列的网格随那一轮的高度角变，走不了门格。
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
        return {{"rows", rowCount_},
                {"gateSpacing", distPerBin_},
                {"minimum", valLo_},
                {"maximum", valHi_},
                {"name", series_[0].name},
                {"title", xAxis_.title},
                {"color", series_[0].color},
                {"rightAxis", yOnRight()}};
    }
    void setConfiguration(const QVariantMap &value)
    {
        if (isComponentComplete())
        {
            qmlWarning(this) << "Chart configuration is creation-only.";
            return;
        }
        const int rows = value.value("rows").toInt();
        const double spacing = value.value("gateSpacing").toDouble();
        const int low = value.value("minimum").toInt(), high = value.value("maximum").toInt();
        if (rows < 1 || !std::isfinite(spacing) || spacing <= 0 || high <= low)
            qFatal("Invalid wind profile configuration.");
        rowCount_ = rows;
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
            points[k] = QPointF(0, k + 0.5);
        xAxis_.lo = low;
        xAxis_.hi = high;
        yAxis_.lo = 0;
        yAxis_.hi = rows;
    }

    // [主] 一帧数据到达：**原地只写值那一列**。
    // **尺寸不对整帧不收**：按短者截断的话，短帧会拼出一条新旧混杂
    // 的曲线。
    Q_INVOKABLE void setData(const QVector<double> &data)
    {
        if (data.size() != rowCount_)
            return;
        QList<QPointF> &points = series_[0].points;
        for (int k = 0; k < rowCount_; ++k)
            points[k].setX(data[k]);
        dataChanged(); // 只换了值；两轴的界不随这一帧数据动
    }

protected:
    // 界恒定：横轴是型号宪法给的量程、纵轴是全部门格，都不随数据 / 视口 / 像素变。
    [[nodiscard]] std::pair<double, double> boundsX(double, double) const override
    {
        return {static_cast<double>(valLo_), static_cast<double>(valHi_)};
    }
    [[nodiscard]] std::pair<double, double> boundsY(double, double) const override
    {
        return {0.0, static_cast<double>(rowCount_)};
    }
    // 界与视口、像素都无关 ⇒ 「满视口下的界宽」就是界宽本身。
    [[nodiscard]] double fullSpanX() const override { return static_cast<double>(valHi_ - valLo_); }
    [[nodiscard]] double fullSpanY() const override { return rowCount_; }

    // 刻度。横轴的视口单位就是要显示的量（m/s 或 dB），照数写。
    [[nodiscard]] QList<AxisText> ticksX(const double lo, const double hi, const int target) const override
    {
        return ChartTicks::decimal(lo, hi, target);
    }
    // 纵轴视口走门格，字写**斜距（米）**：格心 k+½ ↦ 第 k 门的斜距 rangeOf(k)，而 rangeOf 对 k 线性
    // ⇒ 斜距(v) = rangeOf(0) + (v − ½)·门距。
    [[nodiscard]] QList<AxisText> ticksY(const double lo, const double hi, const int target) const override
    {
        return ChartTicks::decimal(lo, hi, target, RangeGate::rangeOf(0, distPerBin_) - 0.5 * distPerBin_,
                                   distPerBin_);
    }

    // 悬停：鼠标落在哪个门格就读哪个门，横线画在格心
    [[nodiscard]] std::optional<AxisText> hoverProbe(const double dataY) const override
    {
        const QList<QPointF> &points = series_[0].points;
        if (points.isEmpty())
            return std::nullopt;
        const int row = std::clamp(static_cast<int>(std::floor(dataY)), 0, rowCount_ - 1);
        const QPointF &p = points[row];
        return AxisText{p.y(), QStringLiteral("斜距  %1 m\n%2  %3")
                                    .arg(RangeGate::rangeOf(row, distPerBin_), 0, 'f', 0)
                                    .arg(series_[0].name)
                                    .arg(p.x(), 0, 'f', 3)};
    }

private:
    const QList<Series> &series() const override { return series_; }
    QList<Series> series_;
    int rowCount_ = 1;
    double distPerBin_ = 1;
    int valLo_ = 0; // 值轴量程（型号宪法给的），界恒等于它
    int valHi_ = 1;
};
