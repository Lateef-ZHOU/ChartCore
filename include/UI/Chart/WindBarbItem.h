#pragma once
#include "UI/Chart/ChartTypes.h"
#include <QVariantMap>
#include <QtQml/qqmlinfo.h>
#include <QtQml/qqml.h>
#include <QtQml/qqmlregistration.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QVector>

#include "Algorithm/Wind/VerticalWindProfile.h"
#include "UI/Chart/Base/ChartThinning.h" // thinIrregular
#include "UI/Chart/Base/ChartTicks.h"    // clock / decimal
#include "UI/Chart/Base/QuickChartItem.h"

// ============================================================
// WindBarbItem.h —— 风羽图（时间 × 高度，每格一根风羽）
//
// 画布那层的活（轴、刻度线、网格、边框、缩放、拖动、主题）全在底座 QuickChartItem。
// 本文件只管三件：存数据、报界与刻度、把风羽吐成顶点。
//
// ── 数据 ─────────────────────────────────────────────────────────────────────────
// 历史是只读垂直风廓线帧的定长环，真实反演与虚拟真值走同一个入口。
// 每帧自带 RxTime、起始高度和层间距和 ENU 向量数组；本图保留同一份额，不拆列、不降为 float。
// 高度由起始高度加下标乘层间距计算，风羽直接使用水平分量；时间仅在投影/画轴时转成秒。
// QML 原样转交整帧，隐藏也收齐历史；GUI 写，场景图同步期间渲染线程只读。
//
// ── 抽稀：两根轴同一套 ───────────────────────────────────────────────────────────────
// 抽稀只为**别糊成一团**——全部风羽是一个几何节点、一次 draw call，几万根也不是性能问题。
// 所以密度没有旋钮：先定一根风羽最少占多少像素（kBarbFootprintMinPx），再由绘图区尺寸现推
// 能画几根。
//
// 四步，横向（位置 = 秒）与纵向（位置 = 米）一字不差：
//   ① 位置落进桶  ② 每桶取**首值**  ③ 画在**桶心**  ④ 空桶不画
//   · **首值，不平均**：风向的算术平均是错的（350° 与 10° 平均出 180°）；改走向量平均的话，
//     向量平均风速与标量平均风速的物理含义又不同，属气象惯例，要问过师兄才能定。
//     首值是唯一不必先回答这个问题的选法。
//   · **画在桶心，不画在自己的位置**：横向各列的时刻不等距，纵向各列的高度网格对不齐——
//     规整化正是靠「落进同一个桶的画在同一处」完成的，独占空间也才立得住。
//   · **空桶不画**：没数据就没风羽，别让插值冒充实测。
//
// 桶长是 **2 的幂、锚在绝对零点、所有列共用**（时间锚 epoch，高度锚 0 米）：
//   · 2 的幂 ⇒ 桶**嵌套**：跨一档，每桶精确裂成两桶 / 两桶精确并成一桶，新画面是旧画面的细化，
//     不重新洗牌 ⇒ 缩放丝滑。（每轴各自 2 合 1；两轴同时跨档才是 4 合 1。）
//   · 锚零点 ⇒ **平移不抖**：桶边界不随视口走，选中哪几列、哪几门与视口位置无关。
//   · 取「像素宽 ≥ kBarbCellMinPx 的最小 2^kBandExp 次幂」⇒ 桶的像素宽恒落在一个**档位带**里
//     （见 kBandExp / kBarbCellMinPx）。「缩小到阈值就合并、放大到阈值就解体」不必单独写，
//     它是这个式子的结果。
//   · 桶长的单位是**位置的单位**（秒、米），不是「几列」「几门」：列的间隔不规则（任务停停开开
//     会留下大洞，按列数分桶会让十小时的停机与一分钟的间隔占一样宽），门的高度差每列不同。
//
// 界：横轴恒 = 一个设计窗宽、锚在最新列的时刻（住基类 QuickChartItem，2026-10-14 用户定；没数据时锚在出生时刻）；
// 纵轴 = 数据范围外扩到整桶。外扩是因为风羽画在桶心、自己还有宽度，界只到数据端点的话最外侧那根露不全。
// 横轴曾按数据范围定界：只有一列时范围零宽、界 = 含它的那一个桶、桶长又随视口跨度走，每钳一次缩几十倍，几次之后
// 横轴退化成一个时刻（10-13 实机所见）——窗宽钉死之后这条路不存在。
// 两根轴都**不设缩放下限**：两根风羽在时间上、高度上能挨多近，事先不知道。
//
// 已知的观感代价：纵向放大到桶比门小、且门高差不是桶长的整数倍时，行距不均匀
// （门高差 145 m、桶 64 m ⇒ 128 / 128 / 128 / 192 米循环）。这是公共桶的固有性质。
//
// ── 独占空间 ─────────────────────────────────────────────────────────────────────
// 数据点 = 杆中点（旋转原点）。风羽绕它转一圈扫出一个圆（半径 = kBarbReach × 杆长），
// 取其外接正方形为「独占空间」，相邻不得干涉。
//   · 两轴各算各的桶长 ⇒ 格子大概率不是正方（如 70×90）；**取较小的那条边**当正方形边长——
//     风羽各向同性，短边放不下就是放不下。
//   · **留缝按比例**（kBarbFill），不按绝对像素：缩放时缝跟着一起放大，观感才稳。
//   · 留缝归绘制器、分桶只管切格子——「独占空间」与「不重叠」是同一个约束，不是两个。
//
// ── 渲染 ─────────────────────────────────────────────────────────────────────────
// 风速 → 羽毛分解：满羽 kFullFeatherSpeed、半羽 kHalfFeatherSpeed，≥ kMaxSpeed 即满羽 6 根并封顶；
// 颜色蓝 → 红随风速升高，≥ kMaxSpeed 恒红；色带是固定色、不归 Theme，画时按当前背景压到看得清（ChartColor，2026-10-14 用户定）。
// **师兄的档位表若有出入，只改这三个常量。**
// 颜色逐根不同，所以用逐顶点着色（QSGVertexColorMaterial），用不了底座的单色节点。
// 粗线自己摊成三角形（RHI 下线宽恒按 1）。
// ============================================================

// ============================================================
// WindBarbItem —— 图本体（自绘 item）
// ============================================================
class WindBarbItem : public QuickChartItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration REQUIRED)
public:
    explicit WindBarbItem(QQuickItem *parent = nullptr)
        : WindBarbItem(QVariantMap{{"heightBins", 1}, {"windowSeconds", 1.0},
            {"columnSeconds", 1.0}, {"maxHeight", 1.0}}, parent) {}

    explicit WindBarbItem(const QVariantMap &configuration, QQuickItem *parent = nullptr)
        : QuickChartItem(kBarbCellMinPx, kBandExp, parent)
    {
        xAxis_.title = QStringLiteral("时间");
        yAxis_.title = QStringLiteral("高度 (m)");
        configure(configuration);
    }
    QVariantMap configuration() const
    {
        return {{"heightBins", heightBins_}, {"windowSeconds", timeWindowSeconds()},
                {"columnSeconds", columnSeconds_}, {"maxHeight", maxHeightM_}};
    }
    void setConfiguration(const QVariantMap &value)
    {
        if (isComponentComplete()) {
            qmlWarning(this) << "Chart configuration is creation-only.";
            return;
        }
        configure(value);
    }

    // [主] 收整帧；数组仍由该只读帧持有，环满仅释放最旧份额。
    Q_INVOKABLE void appendColumn(VerticalWindProfilePtr profile)
    {
        if (!profile || !profile->hasValidGrid()
            || profile->winds.size() < static_cast<std::size_t>(heightBins_)) return;
        const double timeKey = std::chrono::duration<double>(profile->at.time_since_epoch()).count();
        history_.append(std::move(profile));
        noteLastX(timeKey);
        dataChanged();
    }

protected:
    // 全部风羽 = 一个逐顶点着色的几何节点（一次 draw call）
    int drawMarks(QSGNode *clip, int firstIndex, const QRectF &plot) override;

private:
    // ── 纵轴的界：数据范围外扩到整桶（横轴的界住基类：一个设计窗宽、锚在最新列）──────────────────
    // 为什么要外扩：风羽画在桶心、自己还有宽度，而桶心可以比最后一个数据点靠外半个桶——界只到
    // 数据端点的话，拖到头也露不全最外侧那根。外扩之后桶心离界恰好半个桶，而风羽半宽 ≤ 0.46 桶
    // （kBarbFill / 2），够。
    // 桶长按**传进来的那个区间**算：用的就是这一档真正在画的桶长，余量恰好半个桶。
    // 没布局时界立不住，原样交回；没数据时交设计最大高度（0～门数 × 门距）。
    [[nodiscard]] std::pair<double, double> boundsY(const double lo, const double hi) const override
    {
        if (!(hi > lo))
            return {lo, hi};
        if (history_.size() == 0)
            return {0.0, maxHeightM_};
        return bucketedY(lowest(), highest(), hi - lo);
    }

    // ── 满视口下的纵向界宽：缩放的上限 ───────────────────────────────────────────────
    // 「满视口」= 假定视口恰等于**数据全幅**时的界。它只看数据与绘图区像素、**不看当前区间**
    // ⇒ 数据不更新时恒定 ⇒ 缩放钳位一步到位。
    [[nodiscard]] double fullSpanY() const override
    {
        return history_.size() == 0 ? maxHeightM_ : bucketedFullSpanY(lowest(), highest());
    }

    [[nodiscard]] double lowest() const
    {
        double result = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < history_.size(); ++i)
            result = std::min(result, history_.at(i)->startHeightM);
        return result;
    }
    [[nodiscard]] double highest() const
    {
        double result = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < history_.size(); ++i)
            result = std::max(result, history_.at(i)->heightAt(history_.at(i)->winds.size() - 1));
        return result;
    }
    [[nodiscard]] double timeAt(const int column) const
    {
        return std::chrono::duration<double>(history_.at(column)->at.time_since_epoch()).count();
    }

    // 刻度。两根轴的视口单位就是要显示的量：横轴 epoch 秒走钟表阶梯，纵轴米走十进制阶梯。
    [[nodiscard]] QList<AxisText> ticksX(const double lo, const double hi, const int target) const override
    {
        return ChartTicks::clock(lo, hi, target);
    }
    [[nodiscard]] QList<AxisText> ticksY(const double lo, const double hi, const int target) const override
    {
        return ChartTicks::decimal(lo, hi, target);
    }

    // ── 档位带宽：相邻两档相差 2^kBandExp 倍 ─────────────────────────────────────────
    // 改这一个数，档位间隔、像素带宽、格子数区间**全部自洽地跟着变**，别处不用动：
    //   kBandExp = 1 ⇒ 档位 2 的幂，格子像素边长落在 [cellMin, 2·cellMin)，跨档频繁但幅度小
    //   kBandExp = 2 ⇒ 档位 4 的幂，落在 [cellMin, 4·cellMin)，跨档减半但一次 4 合 1
    // 代价：带宽越大，风羽尺寸在带内的摆幅越大（1:4 时 30~120 px），两轴独立取档时
    // 格子长宽比最坏也差这么多。
    static constexpr int kBandExp = 2;

    // 一根风羽要几个「段」（杆 + 羽毛 / 静风标记），每段 6 个顶点。先数后写，两趟。
    [[nodiscard]] static int barbUnits(double speed);
    // 写一根风羽，返回写入的顶点数
    static int writeBarb(QSGGeometry::ColoredPoint2D *v, double x, double y, double speed, const LinAlg::Vec3 &wind,
                         double sizePx, const QColor &color);
    // 一段粗线 = 两个三角形（6 顶点），逐顶点同色
    static int writeSegmentColored(QSGGeometry::ColoredPoint2D *v, double x0, double y0, double x1, double y1,
                                   double half, const QColor &color);
    [[nodiscard]] static QColor colorForSpeed(double speed);

    static constexpr double kMaxSpeed = 24.0;        // ≥此：满羽 + 恒红（6 根满羽 = 24/4）
    static constexpr double kFullFeatherSpeed = 4.0; // 一根满羽代表的风速 (m/s)
    static constexpr double kHalfFeatherSpeed = 2.0; // 一根半羽代表的风速 (m/s)
    // 风羽绕数据点(=杆中点)旋转的最远伸展 / 杆长：尾端满羽 tip 距中点 sqrt((L/2+skew)²+featherLen²)，
    // skew=0.15L、featherLen=0.42L → ≈0.77L；取 0.8 留点余量。独占正方形边长 = 2·kBarbReach·杆长。
    static constexpr double kBarbReach = 0.8;
    static constexpr double kBarbLineWidth = 1.4; // 风羽线宽（像素）

    // ── 可读性判据：一根风羽至少占多大 ─────────────────────────────────────────────────
    // 约束不是"整体看得见"，是**相邻两根羽毛要能分开**——那是最先糊掉的地方。
    //   羽毛与杆的夹角 = atan(featherLen / skew) = atan(0.42 / 0.15) ≈ 70°，沿杆间距 0.15·杆长
    //   ⇒ 相邻羽毛的**垂直**间隔 d = 0.15·sin70°·杆长 ≈ 0.141·杆长。
    //   d ≥ 5 px 能「一眼数清六根」，d ≥ 3 px 才只是「勉强分辨两根」。
    // **30 是用户看实机画面定的**（视觉判断，以看得见的人为准）。它意味着：
    //   · 独占正方形在档位带内是 30~120 px ⇒ 杆长 18.75~75 ⇒ d 恒 ≥ 2.6 px；
    //   · 带的下端低于「数清六根」那条线，落在「分辨得出两根」这一档——换来的是风羽密度。
    // **嫌糊就调大这个数，嫌稀就调小。** 别去动 kBarbReach 或羽毛几何。
    static constexpr double kBarbFootprintMinPx = 30.0;

    // 格子里归风羽的比例，余下均分到四周当留白（0.92 是用户定的：留白 8%）。
    // 按比例而不按绝对像素：缩放时缝跟着一起放大；给绝对值的话放大后风羽涨、缝不涨，看着就挤。
    static constexpr double kBarbFill = 0.92;

    // 桶的像素边长下限 = 独占正方形下限 / 占比。它与 kBandExp 一起在 ctor 里交给基类当格子政策。
    static constexpr double kBarbCellMinPx = kBarbFootprintMinPx / kBarbFill; // ≈ 32.6

    void configure(const QVariantMap &value)
    {
        const int bins = value.value("heightBins").toInt();
        const double window = value.value("windowSeconds").toDouble();
        const double column = value.value("columnSeconds").toDouble();
        const double height = value.value("maxHeight").toDouble();
        const double capacity = std::ceil(kRingOverWindow * window / column);
        if (bins < 1 || !std::isfinite(window) || !std::isfinite(column) || !std::isfinite(height)
            || window <= 0 || column <= 0 || height <= 0 || !std::isfinite(capacity)
            || capacity > std::numeric_limits<int>::max())
            qFatal("Invalid wind barb configuration.");
        heightBins_ = bins;
        columnSeconds_ = column;
        maxHeightM_ = height;
        history_ = VerticalWindProfileHistory(static_cast<std::size_t>(capacity));
        initializeTimeWindow(window);
        yAxis_.lo = 0;
        yAxis_.hi = height;
    }
    int heightBins_ = 1;
    double columnSeconds_ = 1;
    double maxHeightM_ = 1;
    VerticalWindProfileHistory history_{1};

    // 本帧选中的列 / 当前这一列选中的门（都是每桶首值）。只在 drawMarks 里用，clear() 复用容量、
    // 帧内不分配。渲染线程碰，但 updatePaintNode 期间 GUI 线程是阻塞的，与 history_ 同一口径。
    std::vector<int> binPick_;
    std::vector<int> gatePick_;
};

// ============================================================
// WindBarbItem —— inline 实现
// ============================================================

inline QColor WindBarbItem::colorForSpeed(const double speed)
{
    // 蓝→红随风速升高：hue 240(蓝)→0(红) over [0, kMaxSpeed]，过青/绿/黄/橙；≥kMaxSpeed 恒红。
    const double t = std::clamp(speed / kMaxSpeed, 0.0, 1.0);
    return QColor::fromHsvF(static_cast<float>((240.0 / 360.0) * (1.0 - t)), 0.85f, 0.95f);
}

inline int WindBarbItem::barbUnits(const double speed)
{
    if (speed < kHalfFeatherSpeed * 0.5)
        return 2; // 杆 + 静风标记
    const int halfUnits = static_cast<int>(std::lround(std::min(speed, kMaxSpeed) / kHalfFeatherSpeed));
    return 1 + halfUnits / 2 + halfUnits % 2; // 杆 + 满羽 + 半羽
}

inline int WindBarbItem::writeSegmentColored(QSGGeometry::ColoredPoint2D *v, const double x0, const double y0,
                                             const double x1, const double y1, const double half,
                                             const QColor &color)
{
    const auto ax = static_cast<float>(x0);
    const auto ay = static_cast<float>(y0);
    const auto bx = static_cast<float>(x1);
    const auto by = static_cast<float>(y1);
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len = std::hypot(dx, dy);
    // 退化段也占满六个顶点（零面积，画不出东西）。理由与 QuickChartItem::writeSegment 同一条：
    // 返 0 会逼调用方写完再 allocate 收口，而 allocate 丢弃已写数据 —— 整批风羽当场变乱码。
    if (len < 1e-6f)
    {
        const auto r0 = static_cast<uchar>(color.red());
        const auto g0 = static_cast<uchar>(color.green());
        const auto b0 = static_cast<uchar>(color.blue());
        for (int i = 0; i < 6; ++i)
            v[i].set(ax, ay, r0, g0, b0, 255);
        return 6;
    }
    const auto h = static_cast<float>(half);
    const float nx = -dy / len * h;
    const float ny = dx / len * h;
    const auto r = static_cast<uchar>(color.red());
    const auto g = static_cast<uchar>(color.green());
    const auto b = static_cast<uchar>(color.blue());
    v[0].set(ax + nx, ay + ny, r, g, b, 255);
    v[1].set(ax - nx, ay - ny, r, g, b, 255);
    v[2].set(bx + nx, by + ny, r, g, b, 255);
    v[3].set(bx + nx, by + ny, r, g, b, 255);
    v[4].set(ax - nx, ay - ny, r, g, b, 255);
    v[5].set(bx - nx, by - ny, r, g, b, 255);
    return 6;
}

// 一根风羽 = 杆 + 若干羽毛（或静风标记），每样都是一段粗线。
inline int WindBarbItem::writeBarb(QSGGeometry::ColoredPoint2D *v, const double x, const double y,
                                   const double speed, const LinAlg::Vec3 &wind, const double sizePx, const QColor &color)
{
    const double half = kBarbLineWidth / 2.0;
    int n = 0;

    // 杆指向来风：ENU 的东/北分量取反，再转换到屏幕右/下坐标；静风不做除法。
    const double ux = speed > 0.0 ? -wind.x() / speed : 0.0;
    const double uy = speed > 0.0 ? wind.y() / speed : -1.0;
    const double sx = -uy, sy = ux;                     // 羽毛伸出方向（杆的一侧）

    // 旋转原点 = 数据点 = 杆中点：杆头(无羽)、尾端(有羽)各朝相反方向伸出半根 sizePx，
    // 使 0°/90° 等各朝向的风羽都以数据点居中、同列对齐。
    const double halfLen = sizePx * 0.5;
    n += writeSegmentColored(v + n, x - ux * halfLen, y - uy * halfLen, x + ux * halfLen, y + uy * halfLen, half,
                             color);

    // 静风：小方块，无羽毛（几何直出画不了圆；这个尺寸下与小空心圈看着一样）
    if (speed < kHalfFeatherSpeed * 0.5)
    {
        const double r = sizePx * 0.12;
        n += writeSegmentColored(v + n, x - r, y, x + r, y, r, color); // 一段"粗到成方块"的线
        return n;
    }

    // 风速 → 半羽单位（封顶 kMaxSpeed = 满羽）；满羽 = 2 个半羽单位。
    const int halfUnits = static_cast<int>(std::lround(std::min(speed, kMaxSpeed) / kHalfFeatherSpeed));
    const int full = halfUnits / 2;
    const int halfFeather = halfUnits % 2;

    const double featherLen = sizePx * 0.42; // 满羽长度
    const double featherGap = sizePx * 0.15; // 沿杆的羽毛间距
    const double skew = featherGap;          // 羽毛斜指尾端（沿杆分量）
    double pos = halfLen;                    // 从尾端(+halfLen)起，向杆头方向递减

    const auto feather = [&](const double lenScale) {
        const double rootX = x + ux * pos;
        const double rootY = y + uy * pos;
        n += writeSegmentColored(v + n, rootX, rootY, rootX + sx * featherLen * lenScale + ux * skew,
                                 rootY + sy * featherLen * lenScale + uy * skew, half, color);
    };

    for (int i = 0; i < full; ++i)
    {
        feather(1.0);
        pos -= featherGap;
    }
    if (halfFeather)
    {
        if (full == 0)
            pos -= featherGap; // 仅半羽时离尾端留一格（气象惯例）
        feather(0.5);
    }
    return n;
}

inline int WindBarbItem::drawMarks(QSGNode *clip, const int firstIndex, const QRectF &plot)
{
    // 取（必要时新建）逐顶点着色的几何节点
    while (clip->childCount() <= firstIndex)
    {
        auto *fresh = new QSGGeometryNode;
        fresh->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0));
        fresh->setFlag(QSGNode::OwnsGeometry);
        fresh->setMaterial(new QSGVertexColorMaterial);
        fresh->setFlag(QSGNode::OwnsMaterial);
        clip->appendChildNode(fresh);
    }
    auto *node = static_cast<QSGGeometryNode *>(clip->childAtIndex(firstIndex));
    QSGGeometry *geometry = node->geometry();

    // **顶点数为 0 的节点会被批渲染器永久当成空的**（见 QuickChartItem::lineNode 的长注）——绝不给 0：
    // 没东西画时摆三个透明的重合顶点。
    const auto nothingToDraw = [&]() -> int {
        if (geometry->vertexCount() != 3)
            geometry->allocate(3);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        QSGGeometry::ColoredPoint2D *v = geometry->vertexDataAsColoredPoint2D();
        for (int i = 0; i < 3; ++i)
            v[i].set(-1.0f, -1.0f, 0, 0, 0, 0);
        node->markDirty(QSGNode::DirtyGeometry);
        return 1;
    };

    if (history_.size() == 0 || plot.isEmpty())
        return nothingToDraw();

    const double span = xAxis_.span();
    const double ySpan = yAxis_.span();
    if (!(span > 0.0) || !(ySpan > 0.0))
        return nothingToDraw();

    // 桶长按**当前视口**现算，两轴各算各的（规则见文件头「抽稀」）。视口不对齐格子，两端那格被
    // 裁掉一部分是常态，交给裁剪节点收拾——换来的是缩放严格跟鼠标、平移严格跟手。
    const double binSec = binX(span); // 视口单位 = 秒
    const double binM = binY(ySpan);  // 视口单位 = 米
    const auto timeBinOf = [&](const double t) { return std::floor(t / binSec); };

    // 选列：每桶留首列。**按桶边界取、不按视口边界取**——选中哪几列只由数据与桶长决定，与视口
    // 位置无关（按视口边界取的话，拖动时最边上那个半截桶的"首列"会随视口一直换人）。
    // 视口外的列被钳进左右两个哑桶，下面那道交叠判据会把它们筛掉。
    ChartThinning::thinIrregular(
        static_cast<int>(history_.size()), [&](const int c) { return timeAt(c); }, xAxis_.lo, xAxis_.hi, binSec, 1, binPick_);

    // 一根风羽的尺寸：格子取两轴中较小的那条边，其中 kBarbFill 归风羽（见文件头「独占空间」）。
    // max(1.0) 只为绘图区小到放不下一整格的退化情形兜底：宁可画小也不画出格。
    const double cell = std::min(binSec * plot.width() / span, binM * plot.height() / ySpan);
    const double footprint = std::max(1.0, cell * kBarbFill);
    const double barbSize = footprint / (2.0 * kBarbReach);

    // 图元半宽换算回视口单位：判交叠用。横纵各一份（两轴的 值/像素 比例不同）。
    const double halfX = footprint * 0.5 * span / plot.width();
    const double halfY = footprint * 0.5 * ySpan / plot.height();

    // **判据是「图元与视口有交叠」，不是「桶心在视口内」**：按桶心判的话，拖动时边上那根先被裁剪
    // 节点越切越少、桶心一越界就整根蒸发；判交叠则越界的那根照画，由 QSGClipNode 裁掉露在外面的
    // 部分，滑出去是连续的。多画的那一两根全在裁剪区外，不占像素。
    binPick_.erase(std::remove_if(binPick_.begin(), binPick_.end(),
                                  [&](const int c) {
                                      const double mid = (timeBinOf(timeAt(c)) + 0.5) * binSec;
                                      return mid + halfX < xAxis_.lo || mid - halfX > xAxis_.hi;
                                  }),
                   binPick_.end());
    if (binPick_.empty())
        return nothingToDraw();

    // ── 把这一帧要画的风羽逐根走一遍 ─────────────────────────────────────────────────
    // 数顶点与写顶点**共用这一个遍历**：两趟必须选中同一批风羽，筛选各写一遍就是缓冲区溢出的引信。
    // 两轴按公共桶选首值；高度直接由各帧的均匀网格定位。
    const auto eachBarb = [&](auto &&visit) {
        for (const int c : binPick_)
        {
            const auto &profile = *history_.at(c);
            const double x = xAxis_.toPixel((timeBinOf(timeAt(c)) + 0.5) * binSec, plot.left(), plot.right());
            ChartThinning::thinIrregular(static_cast<int>(profile.winds.size()),
                [&](int k) { return profile.heightAt(k); }, yAxis_.lo, yAxis_.hi, binM, 1, gatePick_);
            for (const int k : gatePick_)
            {
                const auto &wind = profile.winds[k];
                if (!wind) continue; // 桶里的首门缺测：这一格不画，不往后找。
                const double s = std::hypot(wind.x(), wind.y());
                if (!std::isfinite(s)) continue;
                const double mid = (std::floor(profile.heightAt(k) / binM) + 0.5) * binM;
                if (mid + halfY < yAxis_.lo || mid - halfY > yAxis_.hi)
                    continue;
                visit(x, yAxis_.toPixel(mid, plot.bottom(), plot.top()), s, wind);
            }
        }
    };

    // 第一趟：数顶点
    int vertices = 0;
    eachBarb([&](double, double, const double s, const LinAlg::Vec3 &) { vertices += barbUnits(s) * 6; });
    if (vertices == 0)
        return nothingToDraw();

    // 第二趟：写顶点
    if (geometry->vertexCount() != vertices)
        geometry->allocate(vertices);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    QSGGeometry::ColoredPoint2D *v = geometry->vertexDataAsColoredPoint2D();
    int n = 0;
    eachBarb([&](const double x, const double y, const double s, const LinAlg::Vec3 &wind) {
        n += writeBarb(v + n, x, y, s, wind, barbSize, legible(colorForSpeed(s))); // 色带是固定色，按当前背景压到看得清
    });
    Q_ASSERT(n == vertices); // 两趟的计数必须一字不差：writeSegmentColored 恒写 6，barbUnits 与 writeBarb 分支一一对上
    node->markDirty(QSGNode::DirtyGeometry);
    return 1;
}
