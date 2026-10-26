#pragma once
#include <algorithm>
#include <cmath>
#include <QColor>

// ============================================================
// ChartColor.h —— 曲线色在任意背景上都看得清：固定色不随主题换，只按对比度往背景的反方向压
//
// 曲线色是数据系列的身份（哪条线是水平角、风羽多快），不是界面语义色，所以不归 Theme、不随深浅色换
// （2026-10-14 用户定）。但同一个固定色在白底和黑底上的可见度差很远：风羽图的色带经过青、绿、黄，白底上的黄
// 几乎是白的；纯蓝在黑底上也暗。于是画之前过一道 legibleOn：与背景的对比度不足 kMinContrast 时，亮底压暗、
// 暗底提亮，直到够为止；够的原样不动，色相不动。对比度按 WCAG 的相对亮度定义，3:1 是图形元件的下限（文字才要 4.5）。
// 纯函数，图表在画的那一刻按当前调色板的背景调它（底座 QuickChartItem::legible）。
// ============================================================
namespace ChartColor
{
inline constexpr double kMinContrast = 3.0;

// sRGB 一个通道 → 线性亮度分量
[[nodiscard]] inline double linear(const int c8)
{
    const double c = c8 / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
[[nodiscard]] inline double luminance(const QColor &c)
{
    return 0.2126 * linear(c.red()) + 0.7152 * linear(c.green()) + 0.0722 * linear(c.blue());
}
[[nodiscard]] inline double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// 亮底压暗（HSV 的 V 每档乘 0.92）、暗底提亮（每档往白混 8%）到对比度 ≥ minRatio；至多 24 档，够不着就交最后一档。
[[nodiscard]] inline QColor legibleOn(QColor c, const QColor &bg, const double minRatio = kMinContrast)
{
    const bool lightBg = luminance(bg) > 0.5;
    for (int i = 0; i < 24 && contrast(c, bg) < minRatio; ++i)
    {
        if (lightBg)
        {
            float h = 0.0f, s = 0.0f, v = 0.0f;
            c.getHsvF(&h, &s, &v);
            c = QColor::fromHsvF(std::max(h, 0.0f), s, v * 0.92f); // 无色相时 getHsvF 交 −1，fromHsvF 不收
        }
        else
        {
            c = QColor(c.red() + (255 - c.red()) * 8 / 100, c.green() + (255 - c.green()) * 8 / 100,
                       c.blue() + (255 - c.blue()) * 8 / 100);
        }
    }
    return c;
}
} // namespace ChartColor
