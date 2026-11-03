#pragma once
#include <QVariantMap>

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

    // 小规格表交 QML 装配；设备不拥有图表或显示缓存。
    static QVariantMap make(int rows, int columns, double spacing,
                                          int windMin, int windMax, double powerMax)
    {
        return {{"rows", rows}, {"columns", columns}, {"gateSpacing", spacing},
                {"windMin", windMin}, {"windMax", windMax}, {"powerMax", powerMax},
                {"cnrMin", kPlt2dCnrMin}, {"cnrMax", kPlt2dCnrMax},
                {"barbWindow", kBarbWindowSec}, {"barbColumn", kBarbColumnSec}};
    }

};
