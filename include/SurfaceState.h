#pragma once
#include <QMatrix4x4>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QMutex>
#include <QMutexLocker>
#include <QtMath>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <memory>

// 行为距离门、列为频点；具体规格由实例的控制器传入。
// 行列数与两条物理轴共同构成创建期配置，只描述网格，不承载谱数据。
struct SurfaceGrid
{
    int rows = 0;
    int columns = 0;
    double rowStart = 0.0;
    double rowStep = 0.0;
    double columnStart = 0.0;
    double columnStep = 0.0;

    int pointCount() const { return rows * columns; }
    [[nodiscard]] double rowPosition(const std::size_t row) const { return rowStart + static_cast<double>(row) * rowStep; }
    [[nodiscard]] double columnPosition(const std::size_t column) const
    {
        return columnStart + static_cast<double>(column) * columnStep;
    }
    [[nodiscard]] bool hasValidAxes() const
    {
        return rows > 0 && columns > 0 && std::isfinite(rowStart) && std::isfinite(rowStep) && rowStep > 0.0
               && std::isfinite(columnStart) && std::isfinite(columnStep) && columnStep > 0.0;
    }
    // 同一网格：行列数与两条轴的起点、步长都相同，不比较谱数据（本类型不承载元素）。
    [[nodiscard]] bool sameAxes(const SurfaceGrid &other) const
    {
        return rows == other.rows && columns == other.columns && rowStart == other.rowStart && rowStep == other.rowStep
               && columnStart == other.columnStart && columnStep == other.columnStep;
    }
    bool operator==(const SurfaceGrid &) const = default;
};

inline qint64 surfaceNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct PreparedSurface
{
    QVector<float> heights;
    quint64 revision = 0;
    SurfaceGrid grid;
    std::shared_ptr<const SurfaceGrid> axes = std::make_shared<const SurfaceGrid>(); // 本帧两条物理轴（行 = 斜距 m、列 = 频率 MHz），只带网格不带元素
    double zMin = 0, zMax = 1;
};

// 曲面、坐标盒、相机与网格共用的世界尺寸；数据点数不决定显示长宽比。
inline const QVector3D kSurfaceSize(2.0f, 2.0f, 1.4f);

inline QVector2D surfaceMeshStep(SurfaceGrid grid, const QVector3D &displaySize)
{
    // 只规定显示密度：较长的水平边约十格。按每个采样间隔的世界长度换算，
    // 取最近整数且不小于一，网线因此落在真实行列上；不随视角或缩放跳档。
    const float spacing = std::max(displaySize.x(), displaySize.y()) / 10.0f;
    return {std::max(1.0f, std::round((grid.columns - 1) * spacing / displaySize.x())),
            std::max(1.0f, std::round((grid.rows - 1) * spacing / displaySize.y()))};
}

struct SurfaceCamera
{
    float yaw = -55.0f;
    float pitch = 35.0f;
    float zoom = 1.0f;

    void rotate(float dx, float dy)
    {
        // 向右拖动，观察方向绕 Z 减小，曲面跟手（用户定）。
        yaw = std::remainder(yaw - dx * 0.4f, 360.0f);
        // 限制上下俯仰，避开视线与世界上向平行的极点；不存 Roll。
        pitch = std::clamp(pitch + dy * 0.4f, -85.0f, 85.0f);
    }

    void scale(float steps) { zoom = std::clamp(zoom * std::pow(1.12f, steps), 0.4f, 4.0f); }

    QMatrix4x4 view() const
    {
        const float az = qDegreesToRadians(yaw), el = qDegreesToRadians(pitch);
        const QVector3D target(0.0f, 0.0f, kSurfaceSize.z() / 2);
        const QVector3D direction(std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el));
        QMatrix4x4 result;
        result.lookAt(target + direction * 6.0f, target, QVector3D(0.0f, 0.0f, 1.0f));
        return result;
    }

    QMatrix4x4 projection(float aspect) const
    {
        // 正交投影让所有 Z 向边保持屏幕竖直；缩放只改视域，不改变上向。
        const float halfHeight = 2.1f / zoom; // 默认视野为刻度文字留白。
        QMatrix4x4 result;
        result.ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, 0.1f, 20.0f);
        return result;
    }
};

struct SurfaceTiming
{
    qint64 syncNs = 0, uploadEnqueueNs = 0, recordNs = 0;
    double delayedWindowGpuMs = 0.0;
};

// 验收探针读计数与耗时。耗时采样默认关闭；业务状态仍只在 synchronize 交接。
struct SurfaceRenderStats
{
    std::atomic<quint64> created{0}, destroyed{0}, draws{0}, uploads{0};
    std::atomic<quint64> revision{0};
    std::atomic<int> backend{0};
    std::atomic<quint64> uploadedBytes{0};
    std::atomic<bool> measuring{false}, timestampsSupported{false};

    void beginMeasurement()
    {
        QMutexLocker lock(&mutex_);
        timings_.clear();
        timings_.reserve(2048);
        measuring = true;
    }
    void recordTiming(SurfaceTiming timing)
    {
        if (!measuring.load())
            return;
        QMutexLocker lock(&mutex_);
        timings_.append(timing);
    }
    QVector<SurfaceTiming> endMeasurement()
    {
        measuring = false;
        QMutexLocker lock(&mutex_);
        return timings_;
    }

private:
    QMutex mutex_;
    QVector<SurfaceTiming> timings_;
};
