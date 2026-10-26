#version 440
layout(location = 0) in float normalizedHeight;
layout(location = 1) in vec2 meshPosition;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform Surface {
    mat4 mvp;
    vec4 worldSize;
    vec4 meshIntervals;
    vec4 lineColor;
} surface;
void main()
{
    // 固定蓝紫—青绿—黄一条色带，颜色只随显示高度变化。
    float t = clamp(normalizedHeight, 0.0, 1.0);
    vec3 low = vec3(0.24, 0.18, 0.52);
    vec3 mid = vec3(0.08, 0.66, 0.61);
    vec3 high = vec3(0.98, 0.88, 0.30);
    vec3 color = t < 0.5 ? mix(low, mid, t * 2.0) : mix(mid, high, (t - 0.5) * 2.0);
    // 网格是曲面颜色的一部分，共用高度、钳值与深度，不叠加共面线几何。
    // 把超采样目标的导数换成最终屏幕像素，线宽与过密淡出不随目标倍率变化。
    vec2 dx = dFdx(meshPosition) * surface.meshIntervals.z;
    vec2 dy = dFdy(meshPosition) * surface.meshIntervals.w;
    vec2 stepPerPixel = max(sqrt(dx * dx + dy * dy), vec2(0.000001));
    vec2 distanceToLine = abs(fract(meshPosition + 0.5) - 0.5) / stepPerPixel;
    vec2 lines = (1.0 - smoothstep(vec2(0.3), vec2(1.1), distanceToLine))
        * (1.0 - smoothstep(vec2(0.12), vec2(0.3), stepPerPixel));
    color = mix(color, surface.lineColor.rgb, surface.lineColor.a * max(lines.x, lines.y));
    fragColor = vec4(color, 1.0);
}
