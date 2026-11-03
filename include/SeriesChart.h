#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <QColor>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGNode>
#include <QString>

#include "ChartThinning.h"
#include "QuickChartItem.h"

// ============================================================
// SeriesChart.h —— **折线族**：把若干条折线画在画布上。
//
// 三层，边界一句话说得清：
//   QuickChartItem —— 画布与导航：轴 / 网格 / 边框 / 悬停线 / 缩放 / 拖动 / 文字层。
//                      **不知道自己画的是什么**，只向子类开口：drawMarks（吐顶点）、
//                      paintMarkLabels（吐字）、hoverProbe、bounds* / fullSpan* / ticks*。
//   SeriesChart    —— 折线族：读取子类点列或分数组剖面、画折线和图例。
//   WindBarbChart  —— 自绘族：读取整帧环形历史、画风羽。与本类**平级**，互不认识。
// 折线是本族的图元，不是底座的内置设施——底座不认识「折线」，风羽图也用不上它。
//
// 三条流的方向（各不相同，且都是有意的）：
//   · 交互（滚轮 / 拖动 / 双击）—— **基类全包，不经虚函数**。滚轮就是滚轮，与画什么无关。
//   · 数据更新 —— 子类改完自己的数据**只调基类一口 `dataChanged()`**，基类自己去问界、重画。
//     只留这一个口，是因为漏调是静默失败：数据进去了、画面不动。
//   · 绘制 —— **基类驱动**，经 drawMarks / paintMarkLabels 让子类补自己的那部分。
// ============================================================
class SeriesChart : public QuickChartItem
{
    Q_OBJECT
    QML_ANONYMOUS
protected:
    // 一条曲线：名字（图例用）、颜色、点列（视口单位）。NaN 值的点自动断开。
    struct Series
    {
        QString name;
        QColor color;
        QList<QPointF> points;
    };

    // **折线族的格子政策：一个像素列**（档位带 [1 px, 2 px)）。整族共用，所以写在这儿
    // 而不是每片叶子各写一遍——叶子只管自己的数据，不管"一根线要占几个像素"。
    explicit SeriesChart(QQuickItem *parent = nullptr)
        : QuickChartItem(1.0, 1, parent)
    {
    }

    // 子类独持数据；底座只读曲线，不开放可写的共享状态。
    virtual const QList<Series> &series() const = 0;
    static constexpr double kLineWidth = 1.5; // 线宽（px）

    // 这一点画不画得出来：**只判数据有效性，不判在不在视口内**。出了视口的点照画，由 QSGClipNode
    // 裁掉露在外面的部分——按视口筛的话，线段只要有一端出界就整段不画，曲线在出界和回界处各断一次。
    // NaN 要判：那是"这一拍没有值"，与"值在视野外"是两回事，前者必须断开。
    [[nodiscard]] static bool usable(const QPointF &p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }

    // ── 折线：每段两个三角形 ──
    // 粗线必须自己摊成三角形（RHI 下 glLineWidth 恒按 1），**不做接头**——采样密、线又细，
    // 缺角看不见。每条曲线一个节点，故返回 series_.size()。
    // **顶点数一次算准、写完不再改**：`writeSegment` 恒写 6 个顶点（退化段写六个重合点），
    // 所以这里不需要、也**绝不能**在写完之后 `allocate` 收口——那会把刚写好的顶点全丢掉。
    int drawMarks(QSGNode *clip, const int firstIndex, const QRectF &r) override
    {
        const auto &series_ = series();
        for (int s = 0; s < series_.size(); ++s)
        {
            const QList<QPointF> &pts = series_[s].points;
            thin(pts, r, pick_);                     // 结果落在 pick_：**活下来的下标**
            drawPolyline(clip, firstIndex + s, r, series_[s].color, pick_,
                         [&](int i) { return pts[i]; });
        }
        return static_cast<int>(series_.size());
    }

    // 点列和分数组剖面共用几何生成；PointAt 只在选中的下标处读取值，不生成 QPointF 缓存。
    template <class Indices, class PointAt>
    void drawPolyline(QSGNode *clip, int index, const QRectF &r, const QColor &color,
                      const Indices &indices, PointAt pointAt)
    {
        int segments = 0;
        for (int j = 1; j < indices.size(); ++j)
            if (usable(pointAt(indices[j - 1])) && usable(pointAt(indices[j]))) ++segments;
        auto *node = lineNode(clip, index, legible(color), segments * 6, QSGGeometry::DrawTriangles);
        if (segments > 0) {
            auto *vertices = node->geometry()->vertexDataAsPoint2D();
            int n = 0;
            for (int j = 1; j < indices.size(); ++j) {
                const QPointF a = pointAt(indices[j - 1]), b = pointAt(indices[j]);
                if (!usable(a) || !usable(b)) continue;
                n += writeSegment(vertices + n,
                    {xAxis_.toPixel(a.x(), r.left(), r.right()), yAxis_.toPixel(a.y(), r.bottom(), r.top())},
                    {xAxis_.toPixel(b.x(), r.left(), r.right()), yAxis_.toPixel(b.y(), r.bottom(), r.top())},
                    static_cast<float>(kLineWidth / 2.0));
            }
        }
        node->markDirty(QSGNode::DirtyGeometry);
    }

protected:
    // ── 抽稀 ────────────────────────────────────────────────────────────────────
    // **每帧的一次投影，不是对数据的变换**：原始点列一个不动，放大回去全都在。
    // 要它的理由是点太密：100 Hz 的回显 × 两分钟 × 四条曲线 ≈ 五万点，每帧重建全部顶点，
    // 渲染线程跟不上。
    //
    // 做法：**一个桶留两点——桶内的首与末**（图元要连成线 ⇒ perBin = 2）。首末保证桶与桶之间
    // 接得上；中间那些点横向挤在一两个像素里，删掉不影响接续。
    // 桶长走基类的 `binX`：2 的幂、锚在绝对零点（本族的格子政策是一个像素列，见 ctor）。
    //   · **平移不抖**：桶边界不随视口走 ⇒ 每个桶选中哪两点与视口位置无关；
    //   · 桶长落在 [1 px, 2 px)，最多两个像素合成一段弦；点数上界 2 × 像素数。
    //
    // **不取桶内的最高 / 最低（不做 M4）**，用户定：那两点保的是「桶内若有大幅来回，包络不丢」，
    // 而在这条数据链上能产生那种来回的只有设备出错——**默认周边设备正常**，不把"揪设备毛病"
    // 做进常态绘制路径。真要查，放大一档就行：抽稀是投影，放大之后每一个原始采样都在那儿。
    // 代价明码：**桶内的单次野值会被这条弦吞掉**，全景下看不见。
    //
    // **前提：x 沿点列单调**（桶的到访顺序 = 数组顺序），否则合并同桶会把不相邻的点揉到一起。
    // 默认时间序列先验一遍，不单调就原样全画。剖面图重写本函数，沿均匀斜距 Y 直接选点。
    virtual void thin(const QList<QPointF> &pts, const QRectF &r, QList<int> &out) const
    {
        constexpr int kPerBin = 2; // 首 / 末（图元要连成线）
        const double plotPx = r.width();
        const double span = xAxis_.hi - xAxis_.lo;
        const bool tooSparse = !(plotPx > 0.0) || !(span > 0.0) || pts.size() <= kPerBin * static_cast<int>(plotPx);
        bool monotone = true;
        for (int i = 1; monotone && i < pts.size(); ++i)
            monotone = !(pts[i].x() < pts[i - 1].x());

        if (tooSparse || !monotone) // 全量：填 0…n-1，调用方只走一条路
        {
            out.resize(pts.size());
            for (int i = 0; i < pts.size(); ++i)
                out[i] = i;
            return;
        }
        const double binWidth = binX(span);
        // 位置交 NaN = 断点：折线在此断开。NaN 可能长在 **y** 上（角度图的世界角反解够不着），
        // 而分桶只看 x，所以这里把"y 无效"翻译成"位置无效"交给它。
        const auto positions = std::views::iota(0, static_cast<int>(pts.size()))
            | std::views::transform([&](int i) {
                return usable(pts[i]) ? pts[i].x() : std::numeric_limits<double>::quiet_NaN();
            });
        ChartThinning::thinIrregular(positions, xAxis_.lo, xAxis_.hi, binWidth, kPerBin, out);
    }

private:
    QList<int> pick_; // [主] 抽稀的落点：**下标**，不是点。每条曲线画完即复用

protected:
    // ── 图例（绘图区右上角）──
    // 单条曲线也显示名称和颜色；只在没有曲线时省略图例。
    void paintMarkLabels(QPainter *painter, const QRectF &r) const override
    {
        const auto &series_ = series();
        if (series_.isEmpty())
            return;
        const QFontMetricsF metrics(painter->font());
        double legendWidth = 0.0;
        for (const auto &s : series_)
            legendWidth = std::max(legendWidth, metrics.horizontalAdvance(s.name));

        const QColor fg = foreground();
        const QColor bg = background();
        const QRectF box(r.right() - legendWidth - 34.0, r.top() + 6.0, legendWidth + 28.0,
                         series_.size() * 16.0 + 8.0);
        painter->fillRect(box, QColor(bg.red(), bg.green(), bg.blue(), 200));
        painter->setPen(QPen(fg, 1));
        painter->drawRect(box);
        for (int i = 0; i < series_.size(); ++i)
        {
            const double y = box.top() + 4.0 + i * 16.0;
            painter->fillRect(QRectF(box.left() + 5.0, y + 6.0, 14.0, 3.0), legible(series_[i].color));
            painter->setPen(fg);
            painter->drawText(QRectF(box.left() + 23.0, y, legendWidth + 2.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter,
                              series_[i].name);
        }
    }
};
