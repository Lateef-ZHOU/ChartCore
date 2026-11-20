#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <span>

#include "AxisMapping.h"
#include "ObjectPolicy.h"

// ============================================================
// DataArrays.h —— 绘图库提供的抽象数据容器：一维、二维、带墙钟时间的一维。
//
// 容器的维度由映射类型确定，元素类型由调用方给出；容器不解释业务含义。
// 数组尺寸在类型上固定，帧不占实例空间存规格；一律禁止复制与移动。
// ============================================================

// 帧的时间必须在类型上不可变；通用 WallClockElement 的时间提取契约不变。
template<class Frame>
concept ImmutableWallClockFrame = requires {
    requires std::same_as<decltype(Frame::at), const std::chrono::system_clock::time_point>;
};

// 一维数组：每个轴点一个元素。
template<MappingLike Axis, class T = double>
class Array1D : private std::array<T, static_cast<std::size_t>(Axis::count)>
{
    using Base = std::array<T, static_cast<std::size_t>(Axis::count)>;
public:
    using AxisType = Axis;
    constexpr Array1D() noexcept : Base{} {}
    DISABLE_COPY_MOVE(Array1D)

    using Base::operator[];
    using Base::size;
    using Base::begin;
    using Base::end;

    static constexpr Axis axis{};
};

// 二维数组：行轴逐行、列轴逐列，行主序；行视图只从左值借用，不保活原数组。
template<MappingLike RowAxis, MappingLike ColumnAxis, class T = double>
class Grid2D : private std::array<T, static_cast<std::size_t>(RowAxis::count) * static_cast<std::size_t>(ColumnAxis::count)>
{
    using Base = std::array<T, static_cast<std::size_t>(RowAxis::count) * static_cast<std::size_t>(ColumnAxis::count)>;
public:
    using RowAxisType = RowAxis;
    using ColumnAxisType = ColumnAxis;
    constexpr Grid2D() noexcept : Base{} {}
    DISABLE_COPY_MOVE(Grid2D)

    using Base::operator[];
    using Base::size;
    using Base::begin;
    using Base::end;

    [[nodiscard]] constexpr std::span<const T> row(const std::size_t index) const & noexcept
    {
        if (index >= static_cast<std::size_t>(RowAxis::count))
            return {};
        return {this->data() + index * static_cast<std::size_t>(ColumnAxis::count),
                static_cast<std::size_t>(ColumnAxis::count)};
    }
    auto row(std::size_t) const && = delete;

    static constexpr RowAxis rowAxis{};
    static constexpr ColumnAxis columnAxis{};
};

// 带墙钟时间的一维数组：位置 = 轴坐标 × 运行时缩放（例如剖面的 sin 仰角），
// 容器只认缩放，不解释它从哪来。
template<MappingLike Axis, class T>
class TimedArray1D : public std::array<T, static_cast<std::size_t>(Axis::count)>
{
    using Base = std::array<T, static_cast<std::size_t>(Axis::count)>;
public:
    using AxisType = Axis;

    const std::chrono::system_clock::time_point at;
    const double scale;

    constexpr TimedArray1D(std::chrono::system_clock::time_point time, double scaleValue) noexcept
        : Base{}, at(time), scale(scaleValue) {}
    DISABLE_COPY_MOVE(TimedArray1D)

    using Base::operator[];
    using Base::size;
    using Base::begin;
    using Base::end;
    using Base::fill;

    [[nodiscard]] double position(const std::size_t index) const noexcept { return Axis::at(index) * scale; }
    [[nodiscard]] double start() const noexcept { return Axis::at(0) * scale; }
    [[nodiscard]] double step() const noexcept requires (Axis::uniform) { return Axis::step * scale; }

    [[nodiscard]] std::span<const T, static_cast<std::size_t>(Axis::count)> values() const & noexcept
    { return std::span<const T, static_cast<std::size_t>(Axis::count)>(this->data(), this->size()); }
    auto values() const && = delete;

    static constexpr Axis axis{};
};
