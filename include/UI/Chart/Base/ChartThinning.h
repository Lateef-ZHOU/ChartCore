#pragma once
#include <algorithm>
#include <cmath>

// ============================================================
// ChartThinning.h —— 抽稀的公用零件：**挑出这一帧要画哪几个**。
//
// 自由函数，不进任何基类——「基类用不到抽稀」因此是编译期事实，不是一句约定：QuickChartItem
// 连这些名字都看不见。基类只调 `drawMarks`；抽不抽、抽几次，是各家 `drawMarks` 体内的直线代码。
//
// 这里的函数**只吃位置、不吃载荷**，吐出的只是「哪些下标活下来」——数据存在哪、怎么存是调用方
// 的事（数据链零搬运）。
//
// **桶长不在这儿算**：「一个格子该多大」是画布的布局问题（要绘图区像素、要图元的最小像素尺寸），
// 归 QuickChartItem::binX / binY；本文件回答的是数据问题「哪几个样本活得下来」。
//
// 位置由调用方读取：折线的点、风羽帧的时刻与逐层高度都沿用原始坐标。
// 桶号 = floor(位置 / 桶长)，锚在绝对零点，两端各放宽一个桶。
// ============================================================
namespace ChartThinning
{

// 不规整序列：吐出活下来的**下标**。
//   locAt(i)  第 i 个的位置；**交 NaN = 这一格是断点**（折线在此断开；连着的断点只吐一个）。
//   perBin    1 = 每桶留首个（图元各自独立，如风羽）；2 = 留首末（图元要连成线，如折线）。
//   out       clear() 后填；QList<int> / std::vector<int> 都行（只用 clear / push_back）。
//
// **视口外的点全钳进左右各一个哑桶**：于是「进视口前的最后一个」与「出视口后的第一个」自然被
// 留下，边界不必另写分支。多留的那两三个交给调用方自己判——折线靠裁剪节点裁掉，风羽靠
// 「图元与视口有没有交叠」那道判据筛掉。
template <class LocAt, class Out>
void thinIrregular(const int count, LocAt locAt, const double lo, const double hi, const double binWidth,
                   const int perBin, Out &out)
{
    out.clear();
    if (count <= 0 || !(binWidth > 0.0))
        return;
    // 桶号是绝对的（位置 / 桶长，锚零点）：epoch 秒除以 0.125 能到 1.4e10——用 double 装，
    // 2^53 以内整数精确，而 int32 装不下。
    const double binLo = std::floor(lo / binWidth) - 1.0;
    const double binHi = std::floor(hi / binWidth) + 1.0;

    double bin = 0.0;
    bool inBin = false;
    bool lastWasBreak = true; // 开头的断点不必吐
    int firstI = -1, lastI = -1;
    const auto flush = [&] {
        if (firstI < 0)
            return;
        out.push_back(firstI);
        if (perBin > 1 && lastI != firstI) // 桶里只有一个时首末重合
            out.push_back(lastI);
        firstI = lastI = -1;
        lastWasBreak = false;
    };
    for (int i = 0; i < count; ++i)
    {
        const double x = locAt(i);
        if (std::isnan(x))
        {
            flush();
            if (!lastWasBreak) // 连着的断点只吐一个：整段够不着时逐个吐等于没抽
            {
                out.push_back(i);
                lastWasBreak = true;
            }
            inBin = false;
            continue;
        }
        const double b = std::clamp(std::floor(x / binWidth), binLo, binHi);
        if (!inBin || b != bin)
        {
            flush();
            bin = b;
            inBin = true;
        }
        if (firstI < 0)
            firstI = i;
        lastI = i;
    }
    flush();
}

} // namespace ChartThinning
