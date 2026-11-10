#pragma once
#include "SurfaceState.h"
#include <optional>

// 绘制层的数值入口。行主序：行 = 距离门，列 = 频点；行列数须与实例网格一致，两条轴随帧给出。
// 短帧（含空帧）补零，长帧只取前 rows*columns 项，绝不续用上一帧的尾部（用户定）。
inline std::optional<PreparedSurface> prepareSurface(const LinearArray2D<double> &values, SurfaceGrid grid,
                                                     double low, double high)
{
    if (grid.rows < 2 || grid.columns < 2 || values.rows != grid.rows || values.columns != grid.columns
        || !values.hasValidAxes() || !std::isfinite(low) || !std::isfinite(high) || high < low)
        return std::nullopt;
    const qsizetype count = grid.pointCount();
    const auto size = static_cast<qsizetype>(values.size());
    const auto valueAt = [&](qsizetype i) { return i < size ? values[static_cast<std::size_t>(i)] : 0.0; };
    const bool automatic = low == high;
    double minimum = valueAt(0), maximum = minimum;
    for (qsizetype i = 0; i < count; ++i) {
        const double value = valueAt(i);
        if (!std::isfinite(value)) return std::nullopt;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (automatic) {
        low = minimum;
        high = maximum;
        // 常量帧仍是一张平面；用零作另一端，全零帧采用 [0,1]，避免坐标与归一化退化。
        if (low == high) {
            low = std::min(0.0, low);
            high = std::max(0.0, high);
            if (low == high) high = 1;
        }
    }
    const double span = high - low;
    if (!std::isfinite(span) || span <= 0) return std::nullopt;
    PreparedSurface result;
    result.grid = grid;
    result.axes = values.axes();
    result.zMin = low;
    result.zMax = high;
    result.heights.resize(count);
    for (qsizetype i = 0; i < count; ++i) {
        const double value = automatic ? valueAt(i) : std::clamp(valueAt(i), low, high);
        result.heights[i] = static_cast<float>((value - low) / span);
    }
    return result;
}
