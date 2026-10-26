#pragma once
#include <algorithm>
#include <cmath>
#include <QDateTime>
#include <QList>
#include <QString>

#include "UI/Chart/Base/QuickChartItem.h" // 只取 AxisText 这个词；依赖是单向的——基类那头看不见本文件

// ============================================================
// ChartTicks.h —— 取刻度的公用零件：两套现成的阶梯（十进制 / 钟表）。
//
// 自由函数，不进任何基类（形制同 ChartThinning.h）。**基类不解析 double**——它对视口值只做比例
// 运算；取刻度却是解析：要先知道「整齐」是对谁而言（十进制的 1/2/5，还是钟表的 15 秒 / 5 分），
// 写文字更要知道这个 double 是什么。所以两样都由各子类在自己的 ticksX / ticksY 里决定，
// 本文件只把用得到的两套阶梯备好给它们调。
//
// 由此**视口用什么单位由子类自己定**——刻度文字不必等于视口值。剖面图的纵轴按门排，视口走门格，
// 字写斜距（米），中间那个仿射只出现在 `decimal` 的两个实参里。
// ============================================================
namespace ChartTicks
{

// 一根轴最多交几条。正常情形远到不了（步长 ≥ 跨度 / target ⇒ 条数 ≤ target + 1）；
// 它挡的是**极深缩放**：步长小到低于 double 在该处的分辨率时，`first + i` 加不动，
// 没有这道上限循环就不会停。
inline constexpr int kMaxPerAxis = 64;

// 「≥ rough 的最小整齐步长」，十进制：1 / 2 / 5 × 10^k。
[[nodiscard]] inline double decimalStep(const double rough)
{
    const double mag = std::pow(10.0, std::floor(std::log10(rough)));
    const double norm = rough / mag;
    return (norm <= 1.0 ? 1.0 : norm <= 2.0 ? 2.0 : norm <= 5.0 ? 5.0 : 10.0) * mag;
}

// 同上，钟表：秒 / 分 / 时的常用档（免得出现"每 37 秒"这种刻度）。入参与返回都是秒。
// 两头都接上十进制阶梯，中间才是钟表档：
//   · 一秒以下按十进制走（0.5 / 0.2 / 0.1 秒……）。时间轴不设缩放下限（用户定：两个采样能挨
//     多近事先不知道），没有这一段的话放大过一秒，轴上就一条刻度都没有了；
//   · 超过一天按「整天数」走——否则步长封顶在一天，跨度一大刻度就成千上万条（一列时间戳是 0
//     的脏数据就够把横轴拉到五十多年宽）。
[[nodiscard]] inline double clockStep(const double roughSec)
{
    if (roughSec < 1.0)
        return std::min(1.0, decimalStep(roughSec));
    static constexpr double kLadder[] = {1,   2,   5,    10,   15,   30,    60,    120,   300,
                                         600, 900, 1800, 3600, 7200, 10800, 21600, 43200, 86400};
    for (const double s : kLadder)
        if (s >= roughSec)
            return s;
    return 86400.0 * decimalStep(roughSec / 86400.0);
}

// ── 十进制刻度 ────────────────────────────────────────────────────────────────────
// 要显示的量与视口单位差一个仿射：**显示值 = offset + scale × 视口值**。
// 刻度取在**显示值**的整齐位置上，再换回视口单位交出去；文字照显示值写。
//   · 视口单位就是要显示的那个量（度、米、m/s、dB）⇒ 两个实参取默认，仿射是恒等；
//   · 门格轴（剖面图纵轴：视口走门格、字写米）⇒ scale = 每格几米，offset = 0 号格左沿对应几米。
// 小数位跟着**步长**走（步长 0.5 带一位、0.02 带两位、≥ 1 不带）：相邻两条刻度的字一定不同，
// 放大到多深都成立。
[[nodiscard]] inline QList<AxisText> decimal(const double lo, const double hi, const int target,
                                             const double offset = 0.0, const double scale = 1.0)
{
    QList<AxisText> out;
    const double a = offset + scale * lo;
    const double b = offset + scale * hi;
    const double shownLo = std::min(a, b); // scale 可以为负（显示值随视口值递减）
    const double shownHi = std::max(a, b);
    const double span = shownHi - shownLo;
    if (!(span > 0.0) || !std::isfinite(span) || target < 2)
        return out;
    const double step = decimalStep(span / target);
    const int decimals = std::clamp(static_cast<int>(std::ceil(-std::log10(step) - 1e-9)), 0, 12);
    const double first = std::ceil(shownLo / step);
    for (int i = 0; i <= kMaxPerAxis; ++i)
    {
        const double shown = (first + i) * step; // 乘出来而不是累加：不积误差，也不会交出 "-0"
        if (shown > shownHi + step * 1e-6)
            break;
        out.append({(shown - offset) / scale, QString::number(shown, 'f', decimals)});
    }
    return out;
}

// ── 钟表刻度 ──────────────────────────────────────────────────────────────────────
// 视口单位 = epoch 秒（墙钟）。文字 HH:mm:ss；步长到了整天那一段改写 MM-dd（那时每条刻度的
// 时分秒都一样，只写时刻等于没写），步长不足一秒时带上毫秒（同理）。
[[nodiscard]] inline QList<AxisText> clock(const double lo, const double hi, const int target)
{
    QList<AxisText> out;
    const double span = hi - lo;
    if (!(span > 0.0) || !std::isfinite(span) || target < 2)
        return out;
    const double step = clockStep(span / target);
    const QString format = step >= 86400.0 ? QStringLiteral("MM-dd")
                           : step < 1.0   ? QStringLiteral("HH:mm:ss.zzz")
                                          : QStringLiteral("HH:mm:ss");
    // 「整齐」是对**本地钟面**而言的：字写的是本地时间，格点就得落在本地的整点上。
    // epoch 秒是 UTC 的，直接对它取整倍数的话，步长一到 3 小时以上刻度就落在 02:00 / 08:00 /
    // 14:00 / 20:00 这种地方（东八区）。先换到本地钟面再取整，取完换回去。
    // 步长整除时差时两种取法是同一组格点，所以一小时以内的步长不受影响。
    const double zone = QDateTime::fromMSecsSinceEpoch(qRound64(lo * 1000.0)).offsetFromUtc();
    const double first = std::ceil((lo + zone) / step);
    for (int i = 0; i <= kMaxPerAxis; ++i)
    {
        const double t = (first + i) * step - zone;
        if (t > hi + step * 1e-6)
            break;
        out.append({t, QDateTime::fromMSecsSinceEpoch(qRound64(t * 1000.0)).toString(format)});
    }
    return out;
}

} // namespace ChartTicks
