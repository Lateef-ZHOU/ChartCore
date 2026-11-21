#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <limits>

// ============================================================
// AxisMapping.h —— 绘图库提供的抽象轴：线性与非线性两种映射。
//
// 映射描述「下标 → 坐标」，帧与图的维度只认映射，不看物理含义。
// 线性映射由起点、步长、点数确定；非线性映射由逐点坐标表确定，表由 constexpr 函数生成、
// 映射内部静态持有。非线性表只要求有限且单调（非降或非增，允许相等）。
//
// 禁止前置声明：类型先定义后使用，概念写在定义之后。
// ============================================================

namespace MappingDetail
{
[[nodiscard]] constexpr bool finite(double value) noexcept
{ return value >= std::numeric_limits<double>::lowest() && value <= std::numeric_limits<double>::max(); }

// 逐点有限且单调：要么整条非降，要么整条非增；相等允许。
template<std::size_t N>
[[nodiscard]] constexpr bool monotonic(const std::array<double, N> &values) noexcept
{
    if (N < 2) return false;
    bool nonDecreasing = true;
    bool nonIncreasing = true;
    for (std::size_t i = 1; i < N; ++i)
    {
        if (!finite(values[i - 1]) || !finite(values[i])) return false;
        nonDecreasing = nonDecreasing && values[i] >= values[i - 1];
        nonIncreasing = nonIncreasing && values[i] <= values[i - 1];
    }
    return nonDecreasing || nonIncreasing;
}
} // namespace MappingDetail

// 映射的公共接口：坐标轴种类 uniform、点数 count、覆盖终点 stop、逐点坐标 at；
// 非线性映射还须带逐点边界 edges。
template<class T>
concept MappingLike = requires(std::size_t index) {
    { T::uniform } -> std::convertible_to<bool>;
    { T::count } -> std::convertible_to<int>;
    { T::stop } -> std::convertible_to<double>;
    { T::at(index) } -> std::same_as<double>;
} && (T::uniform || requires { T::edges; });

// 线性映射：起点、步长、点数是静态模板值。步长是权威值——用覆盖终点反推步长要做除法，
// 既有舍入也说不清末点边界归谁；覆盖终点由乘加导出，是末点的右边界（不含在点内）。
template<double Start, double Step, int Count>
struct LinearMapping
{
    static_assert(Count > 0, "点数须为正");
    static_assert(MappingDetail::finite(Start) && MappingDetail::finite(Step) && Step > 0.0,
                  "轴起点须有限、步长须为正");
    static constexpr bool uniform = true;
    static constexpr double start = Start;
    static constexpr double step = Step;
    static constexpr int count = Count;
    static constexpr int mask = 0; // 遮蔽只影响绘图：从第 mask 个点开始显示，0 = 全显示
    static constexpr double stop = Start + Step * static_cast<double>(Count);
    static_assert(MappingDetail::finite(stop), "轴的覆盖终点须有限");

    [[nodiscard]] static constexpr double at(std::size_t index) noexcept
    { return Start + Step * static_cast<double>(index); }
};

// 非线性映射：逐点坐标由模板实参的 constexpr 函数在编译期生成，映射自己静态持有
// std::array<double, N>（N = 点数 + 1，末项是覆盖终点）；坐标不进类型名，大表也不会撑爆 QML 元类型签名。
template<auto Edges>
struct NonLinearMapping
{
    static constexpr bool uniform = false;
    static constexpr auto edges = Edges();
    static_assert(edges.size() > 1, "非线性映射至少需要首点坐标和覆盖终点");
    static_assert(MappingDetail::monotonic(edges), "非线性映射的坐标须有限且单调（递增或递减）");
    static constexpr int count = static_cast<int>(edges.size()) - 1;
    static constexpr int mask = 0;
    static constexpr double stop = edges.back();

    [[nodiscard]] static constexpr double at(std::size_t index) noexcept { return edges[index]; }
};

// 非单调表映射：逐点值任意，供风速等「值映射」使用；轴仍用 NonLinearMapping 保证单调。
template<auto Table>
struct TableMapping
{
    static constexpr bool uniform = false;
    static constexpr auto edges = Table();
    static_assert(edges.size() > 1, "表映射至少需要两个点");
    static constexpr int count = static_cast<int>(edges.size()) - 1;
    static constexpr int mask = 0;
    static constexpr double stop = edges.back();

    [[nodiscard]] static constexpr double at(std::size_t index) noexcept { return edges[index]; }
};

// 遮蔽轴：仍是同一根轴（count/at/edges 全量保留，数据处理与落盘不受影响），
// 只是绘图从第 Mask 个点开始——镜面之前的距离门不进图，第 Mask 个门成为图上的第 0 门。
template<MappingLike Axis, int Mask>
struct MaskedAxis : Axis
{
    static_assert(Mask >= 0 && Mask < Axis::count, "遮蔽门数必须落在轴内");
    static constexpr int mask = Mask;
};
