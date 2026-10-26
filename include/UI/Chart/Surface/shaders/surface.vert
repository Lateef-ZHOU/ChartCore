#version 440
layout(location = 0) in float height;
layout(location = 1) in vec2 gridPosition;
layout(location = 0) out float normalizedHeight;
layout(location = 1) out vec2 meshPosition;
layout(std140, binding = 0) uniform Surface {
    mat4 mvp;
    vec4 worldSize;
    vec4 meshIntervals;
    vec4 lineColor;
} surface;
void main()
{
    normalizedHeight = height;
    meshPosition = (gridPosition + 1.0) * 0.5 * surface.meshIntervals.xy;
    vec3 worldPosition = vec3(gridPosition * surface.worldSize.xy * 0.5, height * surface.worldSize.z);
    gl_Position = surface.mvp * vec4(worldPosition, 1.0);
}
