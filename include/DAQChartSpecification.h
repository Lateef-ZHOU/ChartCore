#pragma once
#include <QVariantMap>
#include <vector>
#include "AxisMapping.h"

// 只借用型号的静态距离门边界，QML 仅转交此值。
struct StaticRangeEdges
{
    std::vector<double> values; // 遮蔽后的轴坐标（已归零），配置期构造一次
};
Q_DECLARE_METATYPE(StaticRangeEdges)

// 绘图按递增轴定位与抽稀；映射本身允许单调递减，但图页只收递增轴。
template<class T>
concept IncreasingAxis = MappingLike<T> && (T::uniform || T::at(1) > T::at(0));

// 型号交入尺寸与数据范围；公共绘图默认值不属于设备族能力。
struct DAQChartSpecification
{
    // ── 绘图显示范围里**与型号无关**的几条：绘图默认值 ────────
    // CNR ±25 dB 是物理范围，不随卡变。风羽图横轴恒是最近 24 小时（设计窗，图里按它定界）；一列 = 一次风廓线反演 =
    // 一轮波束，名义 20 s 一列（用户定的平均值；轮时长随停稳等待 / 累积时间变）——环长由图按 1.1 × 窗 ÷ 列距算
    // （QuickChartItem::kRingOverWindow），比名义快时窗的最左侧露空、慢时环里比窗更老的列看不到。
    static constexpr int kPlt2dCnrMin = -25;
    static constexpr int kPlt2dCnrMax = 25;
    static constexpr double kBarbWindowSec = 24.0 * 3600.0; // 风羽图的设计时间窗（秒）
    static constexpr double kBarbColumnSec = 20.0;          // 风羽图的名义列距（秒）


    // 小规格表交 QML 装配。静态网格的行是斜距（m），列是频率（MHz）。
    template<class Range, class Frequency> requires IncreasingAxis<Range> && IncreasingAxis<Frequency>
    static QVariantMap make(int windMin, int windMax, double powerMax, int rangeMask = 0)
    {
        static_assert(Frequency::uniform, "绘图列轴暂只支持均匀频率");
        if (rangeMask < 0 || rangeMask >= Range::count) return {};
        const int rows = Range::count - rangeMask;
        const double origin = rangeMask == 0 ? 0.0 : Range::at(rangeMask); // 第 rangeMask 门当图上的 0 m
        QVariantMap result{{"rows", rows}, {"columns", Frequency::count},
                {"freqStart", Frequency::start}, {"freqStep", Frequency::step},
                {"windMin", windMin}, {"windMax", windMax}, {"powerMax", powerMax},
                {"cnrMin", kPlt2dCnrMin}, {"cnrMax", kPlt2dCnrMax},
                {"barbWindow", kBarbWindowSec}, {"barbColumn", kBarbColumnSec},
                {"rangeMask", rangeMask}};
        result.insert("rangeStop", Range::stop - origin);
        if constexpr (Range::uniform)
        {
            result.insert("rangeStart", rangeMask == 0 ? Range::start : 0.0);
            result.insert("gateSpacing", Range::step);
        }
        else
        {
            std::vector<double> edges;
            edges.reserve(static_cast<std::size_t>(rows) + 1);
            for (int i = rangeMask; i <= Range::count; ++i) edges.push_back(Range::edges[i] - origin);
            result.insert("rangeEdges", QVariant::fromValue(StaticRangeEdges{std::move(edges)}));
        }
        return result;
    }

};
