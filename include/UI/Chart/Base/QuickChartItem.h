#pragma once
#include <QSignalBlocker>
#include <QtQml/qqmlregistration.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <QColor>
#include <QDateTime>
#include <QEasingCurve>
#include <QFont>
#include <QFontMetricsF>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QVariant>
#include <QVariantAnimation>
#include <QRectF>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>

#include "UI/Chart/Base/ChartColor.h" // legibleOn：固定色在当前背景上压到看得清
#include <QSGNode>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QWheelEvent>

// ============================================================
// QuickChartItem.h —— 2D 图表的共用底座（Qt Quick 场景图自绘）
//
// **为什么自绘**，三条按硬度排：
// ① **许可证**：Qt Graphs 是「Commercial or GPLv3」，没有 LGPL 这一档（本机文档
//    Docs/Qt-6.11.2/qtgraphs/graphs-2d.html）。QtQuick 本体是 LGPLv3、动态链接可闭源；Qt Graphs
//    一旦对外分发，就要按 GPLv3 交出整个程序的对应源码。QCustomPlot 同样是 GPLv3。
// ② **QCustomPlot 进不了 QML**：只有 QWidget、没有 QQuickItem。（QtCharts 的 QML `ChartView`
//    同病：它是 QQuickPaintedItem 派生，CPU 画完再传纹理。）
// ③ **风羽图那种自定义图元**：Qt Graphs 的 `CustomSeries` 装得下，但它的 delegate 是
//    QQmlComponent——一个数据点一个 QQuickItem 实例，风羽图满库上百万根的量级撑不住。
// ① 就足以否决；②③ 说明即便买了商业授权也还得自己写。轴 / 网格 / 映射 / 缩放这套反正要写一遍
// ⇒ 写完给三张 2D 图共用。
//
// 本文件四件：
//   · ChartAxis       —— 一根轴的显示区间 + 值↔像素的比例映射 + 区间运算（缩放 / 平移 / 钳位）。纯数据。
//   · AxisText        —— 轴上一处的文字（位置 + 字）。刻度与悬停读数都是它。
//   · ChartLabelLayer —— 文字层（QQuickPaintedItem）：刻度文字、轴标题、图例、悬停读数。只在范围 /
//                        数据 / 悬停变化时重画——文字用 QPainter 最省事，密集曲线才值得走几何节点。
//   · QuickChartItem  —— 几何层（QQuickItem）：网格、边框、子类的图元、悬停线，全在 QSGGeometryNode 里；
//                        外加导航（滚轮缩放 / 拖动平移 / 双击复位）。
//
// **本层对 double 只做比例运算，不解析它**：轴的区间用什么单位由子类定（秒、度、米、门格……）。
// 凡是要解析的——界在哪、刻度取在哪、写什么字、悬停读什么值——都是向子类问的口。
//
// **线宽**：RHI 下 glLineWidth 一律按 1 处理，粗线必须自己摊成三角形（每段两个三角形，不做接头——
// 采样密、线又细，缺角看不见）。网格 / 边框这类 1 px 线仍走 DrawLines。
// 抗锯齿靠窗口 surface 的多重采样（main 里给 QSurfaceFormat 的默认值设 samples）。
//
// **主题**：颜色全从本 item 自己的调色板读（QQuickItem::palette 随窗口、随系统主题走，变了发 paletteChanged），
// 不存颜色事实、宿主不必推（见 syncPalette）。
//
// **不可见不画、可见补画，两头都不用自己写**（别加 stale_ 之类的脏标记）：不可见时 Qt 不来画；
// 隐藏期间调 update() 的脏标记由场景图留着，重新可见时补渲染一次。在现宿主（TabPane 的 StackLayout）下
// 实测过：隐藏 → 换数据 → 切回来，切回那一帧与「强制重画一次」的帧像素完全一致。
// ============================================================

// ------------------------------------------------------------
// ChartAxis —— 一根轴：显示区间 + 标题
//
// 区间用什么单位由子类定，本类不问；这里对 double 只做比例运算。取刻度、写刻度文字是解析，
// 不在这儿（见 QuickChartItem::ticksX）。
// ------------------------------------------------------------
struct ChartAxis
{
    double lo = 0.0; // 本轴当前**显示的区间**（两根轴的区间合起来才是视口，别在这儿说"视口"）
    double hi = 1.0;

    // 缩放的下界：放大到此为止（0 = 只受 zoomAt 里那道数值保险约束）。
    // **上界不住这儿**：它是界宽，而界依赖桶长、桶长依赖视口与像素——是个函数，不是一个能存下来的
    // 事实，存下来就一定会陈。由 QuickChartItem 当场向子类问（fullSpan* / bounds*），用完即弃。
    double minSpan = 0.0;

    // 本轴的显示区间归谁管：真 = 跟着界走；假 = 用户自己缩放 / 拖动过，他说了算。
    // **横纵各有一份**——Shift / Ctrl 可以只缩一根轴，所以两轴的归属会不一样。
    // 它是**用户意图**，是货真价实的状态。
    bool follow = true;

    QString title;

    [[nodiscard]] double span() const { return hi - lo; }

    // 只平移、不改跨度地把本轴区间推回界内。
    void slideIntoLimits(const double loLimit, const double hiLimit)
    {
        const double s = span();
        const double full = hiLimit - loLimit;
        // 跨度已经装不下：直接铺满。不这么兜的话下面两个 if 会连发——先贴左界、再贴右界，
        // 结果反而冲出左界。（界本身是格点，所以铺满仍然落在格线上。）
        //
        // **判据带相对容差**：跨度与界宽是经由不同的加减法路径算出来的，浮点上可能差最后一两位。
        // 差这么一点点就走"不铺满"的分支，右端会留下一条 1e-12 量级的缝——看不见，却会让"满幅"
        // 这个状态时有时无，调试时极难缠。
        // （界本身不会一次次漂：桶长取 2 的幂，乘除 2 的幂在二进制浮点里是精确的，见 `bucketed`。）
        if (!(s < full * (1.0 - 1e-12)))
        {
            lo = loLimit;
            hi = hiLimit;
            return;
        }
        if (lo < loLimit)
        {
            lo = loLimit;
            hi = lo + s;
        }
        if (hi > hiLimit)
        {
            hi = hiLimit;
            lo = hi - s;
        }
    }

    // 以 anchor（视口单位）为锚缩放**本轴**：factor < 1 放大（跨度变小）。锚点在区间里的相对位置
    // 不变，所以"鼠标指着哪儿就以哪儿为中心缩放"是 `t` 那一行的结果，不用另写。
    // **只缩，不钳**：跨度上限由调用方先折进 factor（achievableFactor）；位置钳位在缩完之后做——
    // 界要按缩完的区间去问。次序见 wheelEvent。
    void zoomAt(const double anchor, const double factor)
    {
        const double cur = span();
        if (!(cur > 0.0) || !std::isfinite(factor))
            return;
        double s = cur * factor;
        if (minSpan > 0.0)
            s = std::max(s, minSpan); // 放大到此为止
        s = std::max(s, cur * 1e-6);  // 数值保险：不是设计选择，只为挡住跨度塌成 0
        const double t = (anchor - lo) / cur;
        lo = anchor - t * s;
        hi = lo + s;
    }

    // 这一次缩放**本轴实际吃得下多少倍**：跨度被 minSpan 与界宽（full）夹着，吃不下就返回夹过之后
    // 的比例。裸滚轮两轴同缩时，调用方取两轴中**更受限的那个**一起用，横纵比才不会在一根轴触界时走形。
    [[nodiscard]] double achievableFactor(const double factor, const double full) const
    {
        const double cur = span();
        if (!(full > 0.0) || !(cur > 0.0) || !std::isfinite(factor))
            return 1.0;
        double s = cur * factor;
        if (minSpan > 0.0)
            s = std::max(s, std::min(minSpan, full));
        s = std::min(s, full);
        s = std::max(s, full * 1e-6);
        return s / cur;
    }

    // 平移**本轴**的显示区间（跨度不变）。**只挪，不做越界检查**：越界调整本身就是一次平移，
    // 平移若自带检查，就成了「平移 → 检查 → 平移」的互调。钳位归 slideIntoLimits，两者单向依赖。
    // 与方向无关：横轴调它是左右移、纵轴调它是上下移（纵轴那一项不取反，见 mouseMoveEvent）。
    void translate(const double delta)
    {
        if (!std::isfinite(delta) || delta == 0.0)
            return;
        lo += delta;
        hi += delta;
    }

    // 跨度上限到手：把本轴跨度压到它。**跟随态是"设成"不是"压到"**——跟随就是"我要看全"。
    // 居中压缩（而不是保左端）：滚到头那一下，画面对称收拢，比整个往一边甩自然。
    void capSpan(const double cap)
    {
        if (!(cap > 0.0))
            return;
        const double s = span();
        if (!follow && !(s > cap))
            return;
        const double c = (lo + hi) * 0.5;
        lo = c - cap * 0.5;
        hi = c + cap * 0.5;
    }

    // 界到手之后：跟随中就把显示区间贴到界上；否则只把区间推回界内，跨度一个字不改。
    // **不跟随时不许动跨度**：数据一更新就把用户看的那段推着走的话，他想盯住一段过去的数据都盯
    // 不住。数据从区间底下流走就让它流走，显示成空白——诚实，而且区间不会自己漂。
    void applyBounds(const double loLimit, const double hiLimit)
    {
        if (!(hiLimit > loLimit))
            return; // 界还立不住（没数据 / 未布局）：一个字不动
        if (follow || !(span() > 0.0))
        {
            lo = loLimit;
            hi = hiLimit;
            return;
        }
        slideIntoLimits(loLimit, hiLimit);
    }

    // 值 → 像素。pxLo/pxHi 是该轴在控件里的像素两端；纵轴由调用方传反（上小下大）。
    [[nodiscard]] double toPixel(const double v, const double pxLo, const double pxHi) const
    {
        const double s = span();
        return s == 0.0 ? pxLo : pxLo + (v - lo) / s * (pxHi - pxLo);
    }

    [[nodiscard]] double fromPixel(const double px, const double pxLo, const double pxHi) const
    {
        return pxHi == pxLo ? lo : lo + (px - pxLo) / (pxHi - pxLo) * span();
    }
};

// ------------------------------------------------------------
// AxisText —— 轴上一处的文字：位置（**视口单位**）+ 要显示的字。刻度与悬停读数都是它。
// ------------------------------------------------------------
struct AxisText
{
    double at = 0.0;
    QString text;
};

class QuickChartItem;

// ------------------------------------------------------------
// ChartLabelLayer —— 文字层：铺在几何层之上，只画字
// ------------------------------------------------------------
class ChartLabelLayer final : public QQuickPaintedItem
{
    Q_OBJECT
public:
    explicit ChartLabelLayer(QuickChartItem *owner);

    void paint(QPainter *painter) override;

private:
    QuickChartItem *owner_; // 父图表（本层是它的子 item，寿命短于它）
};

// ------------------------------------------------------------
// QuickChartItem —— 画布 + 导航 + 悬停；具体图表继承它
// ------------------------------------------------------------
class QuickChartItem : public QQuickItem
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    // 环容量与设计窗之比（2026-10-13 用户定）：横轴是时间的图，环按名义节拍装 **1.1 个窗**——窗的最左侧才不空。
    // 回显比名义快时最左侧露空、慢时环里比窗更老的点看不到，都不是错。
    static constexpr double kRingOverWindow = 1.1;

    // cellMinPx / bandExp = **本图一个格子的像素政策**（见 binX）。子类出生时就定死，此后不改，
    // 所以走构造函数当 const 成员。
    // 时间窗由创建配置交入；横轴不是时间的图自己覆写 boundsX / fullSpanX。
    explicit QuickChartItem(const double cellMinPx, const int bandExp, QQuickItem *parent = nullptr)
        : QQuickItem(parent), cellMinPx_(cellMinPx > 0.0 ? cellMinPx : 1.0), bandExp_(bandExp > 0 ? bandExp : 1),
          lastX_(QDateTime::currentMSecsSinceEpoch() / 1000.0)
    {
        setFlag(ItemHasContents, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::LeftButton); // 双击复位要它；滚轮的投递也走这道闸
        labels_ = new ChartLabelLayer(this);
        connect(this, &QQuickItem::paletteChanged, this, &QuickChartItem::syncPalette); // 主题变了跟着变
        syncPalette(); // 立起调色板：此后换父项 / 进窗口的重继承归 Qt（它在首次建调色板时接了 parentChanged / windowChanged）
        // 过渡动画：Qt Quick 的动画驱动器按渲染节拍推进，本类不自管定时器。曲线与时长在此定一次；
        // 每帧的值写回两轴（onGlideValue），自然走完接 reconcile（stop 打断不发 finished，不会多落一次）。
        glide_.setDuration(kAnimMs);
        glide_.setEasingCurve(QEasingCurve::OutCubic); // 起手快、收尾慢：缩放看的是"落到哪儿"，收尾慢才看得清落点
        connect(&glide_, &QVariantAnimation::valueChanged, this, &QuickChartItem::onGlideValue);
        connect(&glide_, &QVariantAnimation::finished, this, &QuickChartItem::reconcile);
    }

    // ── 悬停联动（并排两块图共用一根轴时用；QML 连接两边的信号，组件析构自动断连）──
    // 外部推来的坐标只显示、不回发，免得两边互推成环。
    Q_INVOKABLE void applyHover(const double coord)
    {
        hover_ = hoverProbe(coord);
        refresh();
    }
    Q_INVOKABLE void clearHover()
    {
        if (!hover_)
            return;
        hover_.reset();
        refresh();
    }

    // ── 纵轴联动（并排两块图共用纵轴时用）────────────────────────────────────
    // 形制同 applyHover：**外部推来的只吃不回发**，免得两边互推成环。follow 一起带过来，
    // 所以复位（follow=true）与缩放（follow=false）走同一条线，不必另加信号、不必加防环旗。
    Q_INVOKABLE void applyYView(const double lo, const double hi, const bool follow)
    {
        yAxis_.follow = follow;
        yAxis_.lo = lo;
        yAxis_.hi = hi;
        refresh();
    }

    // 时间图纵向排列时共用横轴。连同数据时间锚一起接收，后续布局/缩放仍在同一时间窗内。
    // 不回发联动信号；已有动画只替换横向轨迹，保留本图的纵向动画。
    Q_INVOKABLE void applyXView(const double lo, const double hi, const bool follow, const double anchor)
    {
        xAxis_.lo = lo;
        xAxis_.hi = hi;
        xAxis_.follow = follow;
        lastX_ = anchor;
        if (gliding()) {
            const QSignalBlocker blocker(&glide_);
            auto start = glide_.startValue().toRectF();
            auto end = glide_.endValue().toRectF();
            start.setX(lo); start.setWidth(hi);
            end.setX(lo); end.setWidth(hi);
            glide_.setStartValue(start);
            glide_.setEndValue(end);
        }
        refresh();
    }

private:
    // [主] 双击复位：两轴交还给数据，重新跟随。
    void resetView()
    {
        ChartAxis tx = xAxis_;
        ChartAxis ty = yAxis_;
        tx.follow = ty.follow = true;
        showView(tx, ty); // 跟随态下钳位会把两轴贴到界上
    }

public:
    // 子类往文字层补自己的字（图例之类）。plot = 绘图区。与几何层的 drawMarks 对称：
    // 一个吐顶点、一个吐字。**公开是因为调用者 ChartLabelLayer 是另一个对象**——protected 够不着，
    // 而本仓库不开 friend。
    virtual void paintMarkLabels(QPainter *, const QRectF &) const {}

    // ── 文字层要读的只读口 ──
    [[nodiscard]] const ChartAxis &xAxis() const { return xAxis_; }
    [[nodiscard]] const ChartAxis &yAxis() const { return yAxis_; }
    // 悬停读数：吸附到的位置（落在哪根轴上由 hoverOnY_ 定）+ 要显示的多行文字
    [[nodiscard]] const std::optional<AxisText> &hover() const { return hover_; }
    // 这一帧的刻度。**网格线与文字层都走这一口**，所以两边不可能对不上。
    // 皮下有肉：区间与条数由这里给；落在显示区间之外的丢掉——网格线不在裁剪节点里，
    // 画出绘图区是画布自己的失职，不指望每个子类逐条自查。
    [[nodiscard]] QList<AxisText> xTicks() const
    {
        return inView(ticksX(xAxis_.lo, xAxis_.hi, kXTickTarget), xAxis_);
    }
    [[nodiscard]] QList<AxisText> yTicks() const
    {
        return inView(ticksY(yAxis_.lo, yAxis_.hi, kYTickTarget), yAxis_);
    }
    [[nodiscard]] QColor foreground() const { return fg_; }
    [[nodiscard]] QColor background() const { return bg_; }
    // 曲线色是固定色、不归 Theme（2026-10-14 用户定），画之前过这一道：对比度不足 3:1 时亮底压暗、暗底提亮。
    [[nodiscard]] QColor legible(const QColor &c) const { return ChartColor::legibleOn(c, bg_); }
    [[nodiscard]] bool yOnRight() const { return yOnRight_; }

    // 绘图区（去掉给刻度文字与标题留的边距）。纵轴文字靠右时两侧边距对调。
    [[nodiscard]] QRectF plotRect() const
    {
        const double left = yOnRight_ ? kMarginNarrow : kMarginWide;
        const double right = yOnRight_ ? kMarginWide : kMarginNarrow;
        const QRectF r(left, kMarginTop, width() - left - right, height() - kMarginTop - kMarginBottom);
        return r.isValid() ? r : QRectF();
    }

Q_SIGNALS:
    // 鼠标在本图上移动 / 离开（坐标在吸附轴上）。并排联动时接到对方的 applyHover / clearHover。
    void hoverMoved(double coord);
    void hoverEnded();
    // 本图的**纵轴区间**变了（缩放或复位）。并排共用纵轴时接到对方的 applyYView。
    void yViewChanged(double lo, double hi, bool follow);
    void xViewChanged(double lo, double hi, bool follow, double anchor);

protected:
    static constexpr double kMarginWide = 64.0;   // 有刻度文字与轴标题的那一侧
    static constexpr double kMarginNarrow = 12.0; // 另一侧
    static constexpr double kMarginTop = 12.0;
    static constexpr double kMarginBottom = 46.0;

    // 两根轴的状态是 protected 可写数据，子类在构造期配置：本基类是「基类 protected 只放 const 数据」
    // 那条规矩的明示例外（见协作约束中的绘图轴例外）。
    ChartAxis xAxis_;
    ChartAxis yAxis_;

    // 一个格子至少占多少像素 / 档位带宽 2^bandExp 倍。出生即定（见 ctor）。
    const double cellMinPx_;
    const int bandExp_;
    // 横轴的设计窗（秒）：横轴是时间的图的界恒 = 锚往前这么宽（见 boundsX）；0 = 横轴不是时间（剖面图）。出生即定。
private:
    double windowSec_ = 0.0;
protected:
    [[nodiscard]] double timeWindowSeconds() const { return windowSec_; }
    void initializeInteraction(bool hoverOnY, bool yOnRight)
    {
        hoverOnY_ = hoverOnY;
        yOnRight_ = yOnRight;
    }
    // QML 在组件完成前交入设计窗；运行后不再改变配置。
    void initializeTimeWindow(double seconds)
    {
        windowSec_ = seconds;
        if (seconds > 0)
        {
            const auto [lo, hi] = windowX();
            xAxis_.lo = lo;
            xAxis_.hi = hi;
        }
    }
private:
    // 横轴的锚（epoch 秒）：出生那一刻读一次墙钟立起来，此后只随数据走（noteLastX）；本类读钟的唯一处。
    double lastX_;

    bool hoverOnY_ = false; // true: 悬停吸附在纵轴上、画横线（剖面图这么用）
    bool yOnRight_ = false; // true: 纵轴刻度文字与标题画在右侧（并排两块图镜像摆放）
protected:

    // 「≥ need 的最小 2^(bandExp_·k)」，k 可以是负数。need 非正或非有限时交 1。
    // **不设下限 1**：有的轴 1 以下没意义（单位不可再分的格子），有的轴却非分到 1 以下不可
    // （时间轴上 100 Hz 的数据，桶长 0.125 才合理）。「不可再分」由调用方自己 max(1, …)
    // ——那是调用方对自己单位的知识，不是本函数的。
    [[nodiscard]] double stepAtLeast(const double need) const
    {
        if (!std::isfinite(need) || !(need > 0.0))
            return 1.0;
        return std::exp2(bandExp_ * std::ceil(std::log2(need) / bandExp_));
    }
    // 「每像素几个单位」×「一格至少几像素」=「一格至少几个单位」，再向上取到档位。
    [[nodiscard]] double binFor(const double unitsInView, const double plotPx) const
    {
        return (unitsInView > 0.0 && plotPx > 0.0) ? stepAtLeast(unitsInView * cellMinPx_ / plotPx) : 1.0;
    }

    // 数据 / 范围变了：几何与文字都要重来。
    void refresh()
    {
        update();
        if (labels_)
            labels_->update();
    }

    // ── 数据变了：**唯一入口** ────────────────────────────────────────────────────
    // 子类改完自己的数据只调这一口，基类自己去问界、重画。
    // 只留一个口，是因为漏掉那一步是**静默失败**——数据进去了、画面不动，不报错、不警告。
    // 不收范围当实参：界依赖桶长、桶长依赖视口与像素，不是数据更新那一刻能算定的东西，所以现问
    // （见 boundsX / boundsY）。
    void dataChanged() { reconcile(); }

    // ── 界：**基类不存，现问** ───────────────────────────────────────────────────
    // 给你这条轴的一个区间，你交回 {界下限, 界上限}。界是范围与桶长的函数、桶长又是区间与像素的函数
    // ——它是个函数不是一个事实，谁也不该把它存起来。
    // 传进来的不一定是当前区间：缩放时问的是「缩完之后」的那个（见 wheelEvent）。
    //
    // **横轴是时间的图共用一个实现**（2026-10-14 用户定）：界恒 = [锚 − 设计窗, 锚] 外扩到整桶，锚 = **最新数据的时刻**
    // （lastX_：出生那一刻读一次墙钟立起来，此后只随数据走，见 noteLastX）。于是没数据时是出生那一刻的窗，有数据后是最新
    // 数据的窗，宽度恒等于设计窗；问界不读钟，窗只由数据推着走——缩放、拉窗都不会让窗自己漂。
    // 两版旧做法都否了：按数据范围定界，只有一个图元时范围零宽、界 = 含它的那一个桶、桶长又随视口跨度走，每钳一次缩几十倍，
    // 几次之后横轴退化成一个时刻；按此刻定界（每次问界读钟），窗的更新不受数据驱动、空闲缩放时窗会漂，用户裁"像管理泄露"。
    // 横轴不是时间的图（剖面图）设计窗交 0、自己覆写；纵轴各图范围不同，纯虚不给默认——默认实现不管返什么
    // 都是静默失败（返 {lo, hi} 就冻住缩放、返无穷就拖得出天际），而少写一个编不过。
    [[nodiscard]] virtual std::pair<double, double> boundsX(const double lo, const double hi) const
    {
        Q_ASSERT(windowSec_ > 0.0); // 横轴不是时间却没覆写：编程错误
        if (!(hi > lo))
            return {lo, hi}; // 没布局：界立不住，原样交回
        const auto [t0, t1] = windowX();
        return bucketedX(t0, t1, hi - lo);
    }
    [[nodiscard]] virtual std::pair<double, double> boundsY(double lo, double hi) const = 0;

    // ── 跨度上限：**满视口下的界宽** ──────────────────────────────────────────────
    // 「满视口」= **假定**视口恰等于全幅（时间轴 = 设计窗）时算出来的界。它只拿来算界宽，绝不真把视口设成它。
    // 它不看当前区间 ⇒ 锚不动就恒定 ⇒ 缩放钳位一步到位——这是整套东西不必迭代的全部理由。
    //
    // **它和上面那两口是同一个函数在两个跨度上的取值，别合并**：上限要稳（问满视口），位置边框要
    // 跟着缩放变细（问当前区间），否则细缩放后两端会一直挂着粗档的那截空白。
    // 两者在满视口那一点自动对齐：跨度 = 上限时 `bounds*` 交回的正是满视口的界本身。
    [[nodiscard]] virtual double fullSpanX() const
    {
        Q_ASSERT(windowSec_ > 0.0);
        const auto [t0, t1] = windowX();
        return bucketedFullSpanX(t0, t1);
    }
    [[nodiscard]] virtual double fullSpanY() const = 0;

    // 横轴的框：锚往前一个设计窗（epoch 秒，与盖章同单位）。
    [[nodiscard]] std::pair<double, double> windowX() const { return {lastX_ - windowSec_, lastX_}; }

    // 子类每收一个带时刻的数据就把锚推到它（只前进不后退：墙钟向后跳时窗不跟着退，新点落在窗内偏左）。
    void noteLastX(const double t)
    {
        if (t > lastX_)
            lastX_ = t;
    }

    // ── 格子该多大：**基类算，子类只说视口里有几个单位** ──────────────────────────────
    // 「一个格子占几个单位」= 把「一个格子至少 cellMinPx 像素」这条**布局**要求，按当前的
    // 「单位 / 像素」换算过去，再向上取到档位。单位就是这根轴的视口单位，是什么由子类定，本口不问。
    // 归基类，是因为它要的两样（绘图区像素、图元的最小像素尺寸）都是**画布的事**。
    //
    // **档位带的读法**：声明「一个图元可接受的像素范围」，幂次是推出来的——想要 [50, 200) px
    // 就传 (50, 2)，因为 2 = log2(200/50)。结构上因此钉死「格子是 2 的幂」，而那正是要的：
    //   · **2 的幂 ⇒ 格子嵌套**：跨一档，每格精确裂成两格 / 两格精确并成一格 ⇒ 缩放丝滑；
    //   · **×q 与 ÷q 在二进制浮点里精确**（只改指数位）⇒ 分格算不出误差，整数坐标域不必要。
    [[nodiscard]] double binX(const double unitsInView) const { return binFor(unitsInView, plotRect().width()); }
    [[nodiscard]] double binY(const double unitsInView) const { return binFor(unitsInView, plotRect().height()); }

    // ── 界的统一算法：**把 [t0, t1] 外扩到整桶** ─────────────────────────────────────
    // 这三个 double 是什么单位基类不解释（秒、米、门格都行），只做算术。
    // 桶锚在**绝对零点**（与 ChartThinning 的分桶同一个约定），所以同一个 t 在任何视口下都落在
    // 同一个桶里——缩放时图元不会跳。
    // 用 floor(t1/bin)+1 而不是 ceil(t1/bin)：t1 正好落在桶边界上时 ceil 交的是这根图元所在桶的
    // **左沿**，比桶心还靠里半个桶——偶发且极难查。
    //
    // 「报什么范围」与「bin 怎么定」是**两个独立的旋钮**；界变不变，是它们各自变不变的合成：
    //
    //   轴            范围                        bin                    ⇒ 界
    //   角度图 X      一个设计窗、锚在最新数据      随跨度变（1 px 一档）   变
    //   风羽图 X      一个设计窗、锚在最新数据      随跨度变               变
    //   风羽图 Y      随数据变                    随跨度变               变
    //   角度图 Y      恒定（0..360°）     恒定（≡ 最小单位）     恒定
    //   剖面图 X / Y  恒定（量程 / 门格） 恒定（≡ 最小单位）     恒定
    //
    // 不按桶摆图元的轴按「bin ≡ 最小单位」看待：外扩恒定，式子退化成恒等——那种轴不必真走这一趟，
    // 直接交常量（角度图 Y、剖面图两轴）。
    [[nodiscard]] static std::pair<double, double> bucketed(const double t0, const double t1, const double bin)
    {
        if (!(bin > 0.0))
            return {t0, t1}; // 桶长立不住：原样交回
        const double lo = std::floor(t0 / bin) * bin;
        const double hi = (std::floor(t1 / bin) + 1.0) * bin;
        return {lo, hi > lo ? hi : lo + bin}; // 只有一个图元时给个非零跨度，免得轴退化
    }

    // 两根轴各一个便利口：桶长走那根轴自己的格子政策。
    [[nodiscard]] std::pair<double, double> bucketedX(const double t0, const double t1,
                                                      const double unitsInView) const
    {
        return bucketed(t0, t1, binX(unitsInView));
    }
    [[nodiscard]] std::pair<double, double> bucketedY(const double t0, const double t1,
                                                      const double unitsInView) const
    {
        return bucketed(t0, t1, binY(unitsInView));
    }

    // 满视口下的界宽：**桶长要问两次**——第一次用数据全幅，第二次用"含了外扩的全幅"，
    // 因为外扩本身可能把跨度顶过一条档位线。两问都只吃数据与像素、不吃视口，所以它不是反馈，
    // 只是一个纯函数算两步。
    [[nodiscard]] double bucketedFullSpanX(const double t0, const double t1) const
    {
        return bucketedFullSpan(t0, t1, plotRect().width());
    }
    [[nodiscard]] double bucketedFullSpanY(const double t0, const double t1) const
    {
        return bucketedFullSpan(t0, t1, plotRect().height());
    }

    // ── 刻度：**基类不取、不写，向子类要** ─────────────────────────────────────────
    // 给你这根轴当前的显示区间和「大约要几条」，你交回每条刻度的 {位置（视口单位）, 文字}。
    // 取刻度要先知道「整齐」是对谁而言（十进制的 1/2/5，还是钟表的 15 秒 / 5 分），写文字更要知道
    // 这个 double 是什么——两样都是**解析**，而本类对 double 只做比例运算。
    // 刻度文字因此不必等于视口值：剖面图纵轴视口走门格、字写斜距，靠的就是这个口。
    //
    // **条数归基类**（target）：能摆下几条字是画布的布局问题，同 binX / binY 那条。
    // **纯虚不给默认**：默认交空 = 一根没有刻度的轴，是静默的；少写一个则编不过（同 bounds*）。
    // 现成的两套阶梯（十进制 / 钟表）在 ChartTicks.h，是给子类调的工具——**本类看不见它**。
    [[nodiscard]] virtual QList<AxisText> ticksX(double lo, double hi, int target) const = 0;
    [[nodiscard]] virtual QList<AxisText> ticksY(double lo, double hi, int target) const = 0;

    // 子类给悬停读数：把鼠标处的位置（视口单位）吸附到一个采样点并给出文字；空 = 不显示
    [[nodiscard]] virtual std::optional<AxisText> hoverProbe(double) const { return std::nullopt; }

    // ── 子类的图元：两个对称的口，一个吐顶点、一个吐字 ──────────────────────────────
    // **基类不知道自己画的是折线还是风羽**，它只管画布（轴 / 网格 / 边框 / 悬停线）与导航。
    //
    // drawMarks：在裁剪区内追加几何节点。firstIndex = 头一个可用的子节点下标；
    //   **返回追加了几个**，基类据此摆悬停线并清理上一帧留下的多余节点。
    //   **只收原始量**（裁剪节点、起始下标、绘图区）：要分桶就自己按当前视口去问 binX / binY。
    //   视口不对齐格子，画半截图元由裁剪节点收拾。
    // （另一半 paintMarkLabels 在 public 区，理由见那儿。）
    virtual int drawMarks(QSGNode *, int, const QRectF &) { return 0; }

    // ── **视口的三个落口** ──────────────────────────────────────────────────────
    // 四条链——缩放 / 平移 / 数据更新 / 窗口拉伸——各自只管算"新视口"（或改数据、改绘图区尺寸），
    // 算完落到三个口之一；三个口都先走**同一道钳位 clampView**，再决定怎么画：
    //   · showView —— 直接重绘：拖动、复位。跟手的操作不插值，用户的手压过未走完的过渡（拖动截断动画）。
    //   · glideToView —— 平滑重绘：滚轮。目标一次算定、钳好，动画只负责把显示值推过去。
    //   · reconcile —— 视口没被谁改，只是界可能变了（数据更新 / 窗口拉伸）：原地重钳一次。
    //     过渡进行中**不打断**（角度图每 10 ms 来一口数据，打断就永远走不完），只把目标一并重钳。
    // 于是"越界调整"只有 clampView 一个出口，平移那头不必自带检查（见 ChartAxis::translate）。
    //
    // **一律重画，不做"需不需要"的判断**：走到这儿时都已经有实质变化了。省这一次 update()
    // 换不回什么（Qt 本来就合并重绘请求），却要冒"某种情形下漏画"的险。
    void showView(ChartAxis x, ChartAxis y)
    {
        stopGlide();
        clampView(x, y);
        xAxis_ = x;
        yAxis_ = y;
        Q_EMIT xViewChanged(xAxis_.lo, xAxis_.hi, xAxis_.follow, lastX_);
        Q_EMIT yViewChanged(yAxis_.lo, yAxis_.hi, yAxis_.follow); // 钳位之后再发，对面拿到的才是定稿
        refresh();
    }

    void glideToView(ChartAxis x, ChartAxis y)
    {
        clampView(x, y);
        // 归属当场改：过渡期间若来一口数据，reconcile 按 follow 判"贴界还是推回界内"，
        // 显示值还挂着旧的 follow=true 就会被贴到满幅上去。
        xAxis_.follow = x.follow;
        yAxis_.follow = y.follow;
        // 一段动画只装"从哪到哪"：连滚时停掉正在走的那段，从**此刻显示的**区间起步、朝新目标重起一段——
        // 不排队、不跳变，上一段没走完的行程折进新目标里（目标在 wheelEvent 里已从旧目标起算）。
        glide_.stop();
        glide_.setStartValue(rectOf(xAxis_, yAxis_));
        glide_.setEndValue(rectOf(x, y));
        glide_.start();
    }

    void reconcile()
    {
        const auto previousX = xAxis_;
        if (!plotRect().isEmpty())
        {
            clampView(xAxis_, yAxis_);
            if (gliding())
            {
                ChartAxis tx = targetX();
                ChartAxis ty = targetY();
                clampView(tx, ty);
                glide_.setEndValue(rectOf(tx, ty)); // 改终点不重起：Qt 下一帧按新终点重算插值
            }
        }
        // 仅实际改变的区间参与联动；同一批数据送到其他面板时，不把原样区间反推回动画发起者。
        if (previousX.lo != xAxis_.lo || previousX.hi != xAxis_.hi || previousX.follow != xAxis_.follow)
            Q_EMIT xViewChanged(xAxis_.lo, xAxis_.hi, xAxis_.follow, lastX_);
        refresh();
    }

    // 过渡进行中的那个**目标**视口；没在过渡就是显示的视口。算下一个视口要从它起算——
    // 连滚时上一次没走完的行程才不会丢（显示值还在半路，目标却已经到位）。
    // 目标 = 动画的终点；lo/hi 之外的字段（归属、下限、标题）与显示轴相同（axesOf 从显示轴拷）。
    [[nodiscard]] ChartAxis targetX() const { return gliding() ? axesOf(glide_.endValue().toRectF()).first : xAxis_; }
    [[nodiscard]] ChartAxis targetY() const { return gliding() ? axesOf(glide_.endValue().toRectF()).second : yAxis_; }
    [[nodiscard]] bool gliding() const { return glide_.state() == QAbstractAnimation::Running; }
    void stopGlide() { glide_.stop(); }

    // 两轴区间 ⇄ 动画的一个值。**QRectF 的四个字段当四个数用**（x = 横轴 lo、y = 纵轴 lo、width = 横轴 hi、
    // height = 纵轴 hi），不是几何意义上的矩形：Qt 对 QRectF 逐字段线性插值，四个数各走各的，端点不经加减、
    // 分毫不差；若按真矩形存（width = 跨度），right() 要现算一次 lo + 跨度，端点会差最后一两位。
    [[nodiscard]] static QRectF rectOf(const ChartAxis &x, const ChartAxis &y)
    {
        return QRectF(x.lo, y.lo, x.hi, y.hi);
    }
    // rectOf 的反向：关键帧 → 两轴（lo/hi 取关键帧，其余字段取显示轴）。
    [[nodiscard]] std::pair<ChartAxis, ChartAxis> axesOf(const QRectF &r) const
    {
        ChartAxis x = xAxis_;
        ChartAxis y = yAxis_;
        x.lo = r.x();
        x.hi = r.width();
        y.lo = r.y();
        y.hi = r.height();
        return {x, y};
    }
    // [主·每帧] 过渡中的一拍：把插值写回两根轴、重画。起点、终点与曲线归动画，这里只搬值。
    void onGlideValue(const QVariant &v)
    {
        const QRectF r = v.toRectF();
        const bool xChanged = xAxis_.lo != r.x() || xAxis_.hi != r.width();
        xAxis_.lo = r.x();
        xAxis_.hi = r.width();
        yAxis_.lo = r.y();
        yAxis_.hi = r.height();
        if (xChanged)
            Q_EMIT xViewChanged(xAxis_.lo, xAxis_.hi, xAxis_.follow, lastX_);
        Q_EMIT yViewChanged(yAxis_.lo, yAxis_.hi, yAxis_.follow);
        refresh();
    }

    // 钳位——全仓唯一的一道。问一次界、落一次，**一趟不迭代**是这套东西不成环的全部理由。
    void clampView(ChartAxis &x, ChartAxis &y) const
    {
        // ① 缩放钳位：跨度压到**满视口下的界宽**。这一步与当前区间无关 ⇒ 不会反过来影响②。
        x.capSpan(fullSpanX());
        y.capSpan(fullSpanY());
        // ② 移动钳位：按**压过之后**的区间问界。跨度此刻已定，问出来的界就不会再被改动
        //    ⇒ **一趟走完，没有迭代**。（跟随态下跨度恰等于上限，于是这一问交回的正是满视口
        //    的界，applyBounds 贴上去精确吻合，不会出现"满视口了还能左右滑一点"。）
        const auto [xLo, xHi] = boundsX(x.lo, x.hi);
        const auto [yLo, yHi] = boundsY(y.lo, y.hi);
        x.applyBounds(xLo, xHi);
        y.applyBounds(yLo, yHi);
    }

    // ── 滚轮缩放 ───────────────────────────────────────────────────────────────
    // 裸滚轮 = 两轴同缩；**Shift = 只横向、Ctrl = 只纵向**。锚点是鼠标位置（见 ChartAxis::zoomAt），
    // 不是绘图区中心。
    //
    // 键位按**屏幕方向**绑，不按语义绑：剖面图的横轴是风速、风羽图的横轴是时间，让用户记每张图的
    // 轴语义是不讲理的，"Shift 管左右"到哪张图都成立。哪个键管左右，跟的是外部约定：
    //   · `Shift ≡ 横向` 是 UI 里最硬的一类约定——Windows 资源管理器 / 浏览器 / Office /
    //     VS Code / Figma / Photoshop / Inkscape，macOS 同样，Shift+滚轮一律是横向滚动。
    //   · `Ctrl+滚轮 ≡ 缩放`（浏览器页面缩放、资源管理器图标大小、Excel、VS Code 字号）也是强
    //     关联，但它的语义是"把滚动改成缩放"、**不绑任何轴**；我们裸滚轮已经是缩放，Ctrl 遂是
    //     空出来的那个键，拿去表示"只纵向"不违反任何既有关联。
    //   · **CAD 不是这里的参照系**：SolidWorks / AutoCAD / Cadence 的视图等比，只缩一个轴会把
    //     模型切变，它们根本没有单轴缩放。对的参照族是二维数据图与波形查看器。
    // 那个族里更主流的做法（没做，可叠加，不冲突）：**滚轮悬在哪根轴的刻度条上就只缩那根轴**
    // （Plotly / TradingView / Grafana / DevTools 性能面板）。代价是要在 plotRect 之外的两条
    // 边带上加命中测试。
    void wheelEvent(QWheelEvent *event) override
    {
        const double steps = event->angleDelta().y() / 120.0;
        const QRectF r = plotRect();
        if (steps == 0.0 || r.isEmpty())
        {
            event->ignore();
            return;
        }
        const double factor = std::pow(0.8, steps); // 上滚 = 放大 = 跨度变小
        const Qt::KeyboardModifiers mods = event->modifiers();
        const QPointF p = event->position();

        // 新视口在**目标**视口上算（过渡进行中那是还没走到的那个），算完交 glideToView 去逼近。
        // 连滚不排队：每一次都从此刻显示的区间重新起一段，但行程是累积的——上一次没走完的那段不丢；
        // 只缩一根轴的滚轮也不会把另一根轴的剩余行程取消。锚点也按目标视口换算：几次缩放都指着同一个
        // 像素，复合起来仍是绕那个像素的一次缩放。
        ChartAxis tx = targetX();
        ChartAxis ty = targetY();
        const bool doX = !(mods & Qt::ControlModifier); // Ctrl 摁住 ⇒ 不缩横轴 ⇒ 只纵向
        const bool doY = !(mods & Qt::ShiftModifier);   // Shift 摁住 ⇒ 不缩纵轴 ⇒ 只横向

        // **两轴同缩时锁住横纵比**：给同一个 factor 只是"理论上"比例不变——某一轴触界（到了
        // minSpan 或界宽）就吃不下这个 factor，另一轴还在照吃，比例当场走形。所以先问两轴各自
        // 吃得下多少，取**更保守的那个**（离 1 更近的）一起用。Shift / Ctrl 是明确的单轴缩放，
        // 不参与联动。
        // **跨度上限直接问满视口**：它与当前区间无关 ⇒ 恒定 ⇒ 这一问一步到位，缩放链上没有回头路。
        const double ax = tx.fromPixel(p.x(), r.left(), r.right());
        const double ay = ty.fromPixel(p.y(), r.bottom(), r.top()); // 纵轴像素上小下大
        const double fullX = fullSpanX();
        const double fullY = fullSpanY();

        double f = factor;
        if (doX && doY)
        {
            const double fx = tx.achievableFactor(factor, fullX);
            const double fy = ty.achievableFactor(factor, fullY);
            // **饱和的轴不许否决另一轴**：achievableFactor 对**已经满幅**的轴交回 1.0（一动不能动）；
            // 拿它去做 min/max，就把整个缩放钉死。联动本是为了防比例走形，不该成为否决权——
            // **死掉的控件比比例漂移糟得多**。所以只有两轴都还动得了时才对齐比例；有一轴动不了，
            // 另一轴照常走。
            constexpr double kStuck = 1e-9;
            if (std::abs(fx - 1.0) > kStuck && std::abs(fy - 1.0) > kStuck)
                f = (factor < 1.0) ? std::max(fx, fy) : std::min(fx, fy);
        }
        else if (doX)
            f = tx.achievableFactor(factor, fullX);
        else if (doY)
            f = ty.achievableFactor(factor, fullY);

        // 只缩，不钳：钳位归 glideToView 里那一道 clampView（跨度上限已折进 f，它实质只做移动钳位）。
        if (doX)
        {
            tx.zoomAt(ax, f);
            tx.follow = false;
        }
        if (doY)
        {
            ty.zoomAt(ay, f);
            ty.follow = false;
        }
        glideToView(tx, ty);
        event->accept();
    }

    // ── 拖动平移 ───────────────────────────────────────────────────────────────
    // 抓住图往哪儿拖，数据就往哪儿走 ⇒ 视口朝反方向移。两轴同时跟手，不挂修饰键——
    // 修饰键在滚轮那儿已经是"限制到一轴"的意思，拖动再借一次会打架。
    // **不做框选**：与 3D 那张图的「按住拖 = 转」手势一致，一套肌肉记忆走遍全窗。
    // **不走动画**：拖动本来就跟手，插值只会变成延迟。过渡进行中拖动 = 截断动画、直接画（10-13 用户定：
    // "平移叠加在剩余轨迹上"那版做过——两端同移数据量会失焦、按各自跨度换算又是再补一层，机制不合适就换，不打补丁）。
    // 拖动中滚轮不检测按下与否：新一段从显示值起步，下一次移动再截断它。
    //
    // 拖动时跨度不变 ⇒ 桶长与桶边界都不动，是视口在它们上面滑，所以图元平滑跟手、
    // 只在越过视口边缘时进出。
    void mousePressEvent(QMouseEvent *event) override
    {
        dragFrom_ = event->position();
        dragging_ = true;
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QRectF r = plotRect();
        if (!dragging_ || r.isEmpty())
        {
            event->ignore();
            return;
        }
        const QPointF now = event->position();
        const QPointF delta = now - dragFrom_;
        dragFrom_ = now;

        // **平移链：算新视口 → showView**（钳位与重画都在那儿；showView 先停掉正在走的过渡——拖动截断动画、直接画）。
        // 平移保持跨度不变 ⇒ 桶长不变 ⇒ 界也不变，所以那一道钳位在这条链上实质只做移动钳位。
        ChartAxis tx = xAxis_;
        ChartAxis ty = yAxis_;
        if (delta.x() != 0.0)
        {
            tx.translate(-delta.x() * tx.span() / r.width());
            tx.follow = false;
        }
        if (delta.y() != 0.0)
        {
            // 纵轴像素上小下大（toPixel(v, bottom, top)），所以这一项**不**取反。
            ty.translate(delta.y() * ty.span() / r.height());
            ty.follow = false;
        }
        showView(tx, ty);
        event->accept();
    }

    // **松手不带惯性**（10-13 用户定：做过一版、实机看过，鼠标上得养成"动着松手"的习惯才有效果，不适合桌面，删了）。
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragging_ = false;
        reconcile(); // 松手再落一次核心（跨度没变 ⇒ 桶长不变，多半只是一次空钳位）
        event->accept();
    }

    // 双击 = 复位（两轴交还给数据）。没做成按钮：图上本来就没地方摆，双击是图表的通用手势。
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        dragging_ = false; // 双击前那一下 press 已经把 dragging_ 打开了，别让它留着
        resetView();
        event->accept();
    }

    void hoverMoveEvent(QHoverEvent *event) override
    {
        const QRectF r = plotRect();
        if (r.isEmpty() || !r.contains(event->position()))
        {
            clearHover();
            Q_EMIT hoverEnded();
            return;
        }
        const double coord = hoverOnY_ ? yAxis_.fromPixel(event->position().y(), r.bottom(), r.top())
                                       : xAxis_.fromPixel(event->position().x(), r.left(), r.right());
        applyHover(coord);
        Q_EMIT hoverMoved(coord);
    }

    void hoverLeaveEvent(QHoverEvent *) override
    {
        clearHover();
        Q_EMIT hoverEnded();
    }

    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override
    {
        QQuickItem::geometryChange(newGeometry, oldGeometry);
        if (labels_)
            labels_->setSize(newGeometry.size());
        // **窗口拉伸是第四个重画源**（另三个：缩放、平移、数据更新）：桶长的入参之一就是绘图区像素，
        // 像素一变档位就可能换，画面内容是真的不一样了——不是"重画一遍一样的东西"。
        // 还有个时序上的理由：数据可能在布局之前就灌进来，那一拍 plotRect() 还是空的、drawMarks
        // 直接返回；此后若无新数据，就靠这一次补画。
        reconcile(); // 它自己会 refresh
    }

    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

    // ── 给子类画图元用的两个原语 ──
    // 无状态的静态工具，不是可变点，drawMarks 的实现者非用不可。

    // 取（必要时新建）parent 下第 index 个几何节点，并设好颜色与顶点数。
    // **按下标取而不是记成员指针**：场景图随时可能销毁整棵节点树，记指针就要处理悬空。
    static QSGGeometryNode *lineNode(QSGNode *parent, int index, const QColor &color, int vertexCount,
                                     QSGGeometry::DrawingMode mode);

    // 写一段粗线（两个三角形）。**恒返 6**——退化段写六个重合顶点，理由见实现处那段注。
    static int writeSegment(QSGGeometry::Point2D *v, const QPointF &a, const QPointF &b, float half);

private:
    // [主] 颜色全从本 item 自己的调色板解析：底色 Base、前景 WindowText，网格 = 两者按 0.72 / 0.28 调出的一档灰
    // （两种主题下都不抢眼）。QQuickPalette 是 Qt 的私有类型，只经属性系统读，不拉私有头。
    // 照 Qt 自己的 Item 做：构造里读一次把调色板立起来，重继承（换父项 / 进窗口 / 应用调色板变）全由 Qt 经
    // QQuickPalette::changed → paletteChanged 送到这里；本类不另判时机。
    void syncPalette()
    {
        auto *pal = qvariant_cast<QObject *>(property("palette"));
        bg_ = pal->property("base").value<QColor>();
        fg_ = pal->property("windowText").value<QColor>();
        grid_ = QColor(qRound(bg_.red() * 0.72 + fg_.red() * 0.28), qRound(bg_.green() * 0.72 + fg_.green() * 0.28),
                       qRound(bg_.blue() * 0.72 + fg_.blue() * 0.28));
        refresh();
    }
    QColor bg_, fg_, grid_; // 由 syncPalette 解析，不设出生值
    std::optional<AxisText> hover_;

    // bucketedFullSpanX / Y 的本体：两根轴只差绘图区在那个方向上的像素数。
    [[nodiscard]] double bucketedFullSpan(const double t0, const double t1, const double plotPx) const
    {
        const double span = t1 - t0;
        if (!(span > 0.0))
            return 0.0; // 交 0 = 不设上限（capSpan 会跳过）
        double bin = binFor(span, plotPx);
        bin = binFor(span + 2.0 * bin, plotPx);
        const auto [lo, hi] = bucketed(t0, t1, bin);
        return hi - lo;
    }

    // 目标刻度条数（子类按整齐步长取，略有出入）。画布的布局政策，经 ticksX / ticksY 的形参交出去。
    static constexpr int kXTickTarget = 6;
    static constexpr int kYTickTarget = 8;
    // 只留落在显示区间里的（留一点浮点余量：恰在端点上的那条经过换算可能差最后一位）。
    [[nodiscard]] static QList<AxisText> inView(QList<AxisText> ticks, const ChartAxis &axis)
    {
        const double slack = axis.span() * 1e-6;
        ticks.removeIf([&](const AxisText &t) { return !(t.at >= axis.lo - slack && t.at <= axis.hi + slack); });
        return ticks;
    }
    QPointF dragFrom_;      // 上一次鼠标位置（拖动平移用逐帧增量，不用按下点，免得累积误差）
    bool dragging_ = false;

    // 缩放过渡动画：一段"从哪到哪"（值见 rectOf），Qt Quick 的动画驱动器按渲染节拍推进；终点就是目标视口（钳过），
    // 过渡中它就是"当前视口"，下一次缩放从它起算（targetX / targetY）。
    static constexpr int kAnimMs = 160;
    QVariantAnimation glide_;
    ChartLabelLayer *labels_ = nullptr; // 子 item，Qt 对象树接管
};

// ============================================================
// inline 实现
// ============================================================

inline QSGGeometryNode *QuickChartItem::lineNode(QSGNode *parent, const int index, const QColor &color,
                                                 const int vertexCount, const QSGGeometry::DrawingMode mode)
{
    while (parent->childCount() <= index)
    {
        auto *fresh = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        fresh->setGeometry(geometry);
        fresh->setFlag(QSGNode::OwnsGeometry);
        fresh->setMaterial(new QSGFlatColorMaterial);
        fresh->setFlag(QSGNode::OwnsMaterial);
        parent->appendChildNode(fresh);
    }
    auto *node = static_cast<QSGGeometryNode *>(parent->childAtIndex(index));
    auto *material = static_cast<QSGFlatColorMaterial *>(node->material());
    if (material->color() != color)
    {
        material->setColor(color);
        node->markDirty(QSGNode::DirtyMaterial);
    }
    // **绝不让一个节点的顶点数为 0**（整条路上最坑的一步）：批渲染器把"出生时 0 顶点"的节点当成
    // 空的记下了，此后 allocate 再多顶点、再 markDirty 也不画。现象极具迷惑性——网格与边框（出生
    // 就有顶点）一切正常，曲线（头几帧还没数据）永远不出现，而调试打印里顶点数、坐标、颜色全是对的。
    // 修法只能是"别变空"：要 0 个顶点时摆两个重合顶点到画布外（零长度线，画不出东西）。
    // **不要改成"换一块新几何再 delete 旧的"**——渲染器本帧还引着旧几何，当场堆损坏。
    const bool empty = vertexCount <= 0;
    const int count = empty ? 2 : vertexCount;
    QSGGeometry *geometry = node->geometry();
    if (geometry->vertexCount() != count)
        geometry->allocate(count);
    geometry->setDrawingMode(empty ? QSGGeometry::DrawLines : mode);
    if (empty)
    {
        QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
        v[0].set(-1.0f, -1.0f);
        v[1].set(-1.0f, -1.0f);
    }
    return node;
}

inline int QuickChartItem::writeSegment(QSGGeometry::Point2D *v, const QPointF &a, const QPointF &b, const float half)
{
    const auto ax = static_cast<float>(a.x());
    const auto ay = static_cast<float>(a.y());
    const auto bx = static_cast<float>(b.x());
    const auto by = static_cast<float>(b.y());
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len = std::hypot(dx, dy);
    // **退化段也要占满六个顶点**：若返 0，调用方就得写完再 `allocate(实际数)` 收口——而
    // `QSGGeometry::allocate` 会**丢弃**已有数据，那一收等于把整条曲线刚写好的顶点全抹成未初始化
    // 内存，画出来是满屏乱三角形、且每帧不同。两个采样落在同一个像素上就足以造出一个退化段。
    // 所以顶点数**恒等于预估数**：退化段写六个重合顶点（零面积，画不出东西）。
    if (len < 1e-6f)
    {
        for (int i = 0; i < 6; ++i)
            v[i].set(ax, ay);
        return 6;
    }
    const float nx = -dy / len * half;
    const float ny = dx / len * half;
    v[0].set(ax + nx, ay + ny);
    v[1].set(ax - nx, ay - ny);
    v[2].set(bx + nx, by + ny);
    v[3].set(bx + nx, by + ny);
    v[4].set(ax - nx, ay - ny);
    v[5].set(bx - nx, by - ny);
    return 6;
}

// 节点结构（固定顺序，按下标取）：0 = 网格，1 = 边框，2 = 裁剪节点（其下：子类的图元 + 悬停线）
inline QSGNode *QuickChartItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    const QRectF r = plotRect();
    QSGNode *root = oldNode;
    if (!root)
        root = new QSGNode;
    if (r.isEmpty())
        return root;

    // ── 网格：刻度位置的细线 ──
    // 位置向子类要（见 ticksX / ticksY）；这里只用位置，文字归文字层。
    const QList<AxisText> xMarks = xTicks();
    const QList<AxisText> yMarks = yTicks();
    {
        auto *node = lineNode(root, 0, grid_, static_cast<int>(xMarks.size() + yMarks.size()) * 2,
                              QSGGeometry::DrawLines);
        QSGGeometry::Point2D *v = node->geometry()->vertexDataAsPoint2D();
        int n = 0;
        for (const AxisText &t : xMarks)
        {
            const auto x = static_cast<float>(xAxis_.toPixel(t.at, r.left(), r.right()));
            v[n++].set(x, static_cast<float>(r.top()));
            v[n++].set(x, static_cast<float>(r.bottom()));
        }
        for (const AxisText &t : yMarks)
        {
            const auto y = static_cast<float>(yAxis_.toPixel(t.at, r.bottom(), r.top()));
            v[n++].set(static_cast<float>(r.left()), y);
            v[n++].set(static_cast<float>(r.right()), y);
        }
        node->markDirty(QSGNode::DirtyGeometry);
    }

    // ── 边框：四边 ──
    {
        auto *node = lineNode(root, 1, fg_, 8, QSGGeometry::DrawLines);
        QSGGeometry::Point2D *v = node->geometry()->vertexDataAsPoint2D();
        const auto l = static_cast<float>(r.left());
        const auto t = static_cast<float>(r.top());
        const auto rr = static_cast<float>(r.right());
        const auto b = static_cast<float>(r.bottom());
        v[0].set(l, t);
        v[1].set(rr, t);
        v[2].set(rr, t);
        v[3].set(rr, b);
        v[4].set(rr, b);
        v[5].set(l, b);
        v[6].set(l, b);
        v[7].set(l, t);
        node->markDirty(QSGNode::DirtyGeometry);
    }

    // ── 裁剪节点：子类的图元与悬停线都关在绘图区内 ──
    // 裁剪节点自己也要有几何（只设 setClipRect 不给几何是不够的）。
    while (root->childCount() <= 2)
    {
        auto *fresh = new QSGClipNode;
        fresh->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4));
        fresh->setFlag(QSGNode::OwnsGeometry);
        fresh->setIsRectangular(true);
        root->appendChildNode(fresh);
    }
    auto *clip = static_cast<QSGClipNode *>(root->childAtIndex(2));
    QSGGeometry::updateRectGeometry(clip->geometry(), r);
    clip->setClipRect(r);
    clip->markDirty(QSGNode::DirtyGeometry);

    // ── 子类的图元：**基类不知道那是折线还是风羽**，只给一块裁剪区和一个起始下标 ──
    const int marks = drawMarks(clip, 0, r);

    // ── 悬停线：排在图元**之后** ⇒ 画在图元之上。它是交互反馈，不该被内容盖住 ──
    {
        auto *node = lineNode(clip, marks, fg_, hover_ ? 2 : 0, QSGGeometry::DrawLines);
        if (hover_)
        {
            QSGGeometry::Point2D *v = node->geometry()->vertexDataAsPoint2D();
            if (hoverOnY_) // 吸附在纵轴上 → 画一条横线
            {
                const auto y = static_cast<float>(yAxis_.toPixel(hover_->at, r.bottom(), r.top()));
                v[0].set(static_cast<float>(r.left()), y);
                v[1].set(static_cast<float>(r.right()), y);
            }
            else
            {
                const auto x = static_cast<float>(xAxis_.toPixel(hover_->at, r.left(), r.right()));
                v[0].set(x, static_cast<float>(r.top()));
                v[1].set(x, static_cast<float>(r.bottom()));
            }
        }
        node->markDirty(QSGNode::DirtyGeometry);
    }
    // 上一帧可能留下更多节点（图元变少），多余的收成空
    for (int i = marks + 1; i < clip->childCount(); ++i)
        lineNode(clip, i, fg_, 0, QSGGeometry::DrawLines);

    return root;
}

// ------------------------------------------------------------
// ChartLabelLayer —— 刻度文字 / 轴标题 / 图例 / 悬停读数
// ------------------------------------------------------------
inline ChartLabelLayer::ChartLabelLayer(QuickChartItem *owner) : QQuickPaintedItem(owner), owner_(owner)
{
    setAcceptedMouseButtons(Qt::NoButton); // 只显示，不吃事件——悬停归几何层
    setAntialiasing(true);
    setZ(1.0);
}

inline void ChartLabelLayer::paint(QPainter *painter)
{
    const QRectF r = owner_->plotRect();
    if (r.isEmpty())
        return;
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    const QColor fg = owner_->foreground();
    painter->setPen(fg);
    QFont font = painter->font();
    font.setPointSizeF(9.0);
    painter->setFont(font);
    const QFontMetricsF metrics(font);

    // 纵轴刻度（默认右对齐贴着绘图区左沿；靠右摆时改成左对齐贴着右沿）
    const bool yRight = owner_->yOnRight();
    for (const AxisText &t : owner_->yTicks())
    {
        const double y = owner_->yAxis().toPixel(t.at, r.bottom(), r.top());
        const QRectF box = yRight ? QRectF(r.right() + 6.0, y - 8.0, width() - r.right() - 6.0, 16.0)
                                  : QRectF(0.0, y - 8.0, r.left() - 6.0, 16.0);
        painter->drawText(box, (yRight ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, t.text);
    }
    // 横轴刻度（居中挂在绘图区下沿）。两端那个往里收，免得被控件边界切半或跟邻座的图挤在一起。
    for (const AxisText &t : owner_->xTicks())
    {
        const double x = owner_->xAxis().toPixel(t.at, r.left(), r.right());
        QRectF box(x - 40.0, r.bottom() + 4.0, 80.0, 16.0);
        if (box.left() < 0.0)
            box.moveLeft(0.0);
        if (box.right() > width())
            box.moveRight(width());
        painter->drawText(box, Qt::AlignHCenter | Qt::AlignTop, t.text);
    }
    // 轴标题
    if (!owner_->xAxis().title.isEmpty())
        painter->drawText(QRectF(r.left(), r.bottom() + 22.0, r.width(), 18.0), Qt::AlignHCenter | Qt::AlignTop,
                          owner_->xAxis().title);
    if (!owner_->yAxis().title.isEmpty())
    {
        painter->save();
        painter->translate(yRight ? width() - 14.0 : 14.0, r.center().y());
        painter->rotate(-90.0);
        painter->drawText(QRectF(-r.height() / 2.0, -9.0, r.height(), 18.0), Qt::AlignCenter, owner_->yAxis().title);
        painter->restore();
    }

    // 子类的字（图例之类）。**基类不知道有"图例"这回事**——图例是折线族的东西，
    // 住 SeriesChart。这一口与几何层的 drawMarks 对称：一个吐顶点、一个吐字。
    owner_->paintMarkLabels(painter, r);

    // 悬停读数（绘图区左上角）
    if (const auto &hover = owner_->hover())
    {
        const QStringList lines = hover->text.split(QLatin1Char('\n'));
        double textWidth = 0.0;
        for (const QString &line : lines)
            textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
        const QRectF box(r.left() + 6.0, r.top() + 6.0, textWidth + 12.0, lines.size() * 15.0 + 8.0);
        const QColor ink = owner_->foreground(); // 反色底：前景色当底、底色当字，两种主题下都读得清
        painter->fillRect(box, QColor(ink.red(), ink.green(), ink.blue(), 220));
        painter->setPen(owner_->background());
        painter->drawText(box.adjusted(6.0, 4.0, -6.0, -4.0), Qt::AlignLeft | Qt::AlignTop, hover->text);
    }
}
