#pragma once
#include "SurfaceState.h"
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLineF>
#include <QPainter>
#include <QPolygonF>
#include <QQuickPaintedItem>
#include <QTransform>
#include <QVariant>
#include <array>
#include <limits>

// 原型坐标注记：文字与宿主轴平行、字顶朝轴，平面绕轴尽量朝向相机（用户定）。
// 只画边界上的三根轴，不叠加穿过曲面的背面网格，也不参与鼠标拾取。
namespace SurfaceAxes {
struct Scale { QString title; double low, high; QVector<double> ticks; };
inline constexpr float kTextWorldUnit = 0.01f; // 字体坐标统一换成世界长度，保留侧视压缩与缩放。
struct Projection {
    QMatrix4x4 matrix;
    QSizeF size;
    QVector3D towardCamera;

    Projection(const SurfaceCamera &camera, const QSizeF &viewport) : size(viewport)
    {
        const auto view = camera.view();
        matrix = camera.projection(static_cast<float>(size.width() / size.height())) * view;
        towardCamera = QVector3D(view(2, 0), view(2, 1), view(2, 2));
    }
    QPointF point(const QVector3D &world) const
    {
        const auto ndc = matrix.map(world);
        return {(ndc.x() + 1) * size.width() / 2, (1 - ndc.y()) * size.height() / 2};
    }
    QPointF vector(const QVector3D &world) const
    {
        const auto ndc = matrix.mapVector(world);
        return {ndc.x() * size.width() / 2, -ndc.y() * size.height() / 2};
    }
};
struct Axis {
    QVector3D start, end, right, down;
    bool hasTextPlane = false;
};
struct Line { QVector3D start, end; };
struct Label {
    QString text;
    QSizeF size;
    QVector3D center, right, down;
    int axis;
    bool title;

    QRectF localBounds() const { return {QPointF(-size.width() / 2, -size.height() / 2), size}; }
    QVector3D worldAt(const QPointF &p) const
    {
        return center + right * static_cast<float>(p.x()) + down * static_cast<float>(p.y());
    }
};
struct ProjectedLabel {
    Label object;
    QTransform transform;
    QPolygonF footprint(qreal padding = 0) const
    {
        return transform.map(QPolygonF(object.localBounds().adjusted(-padding, -padding, padding, padding)));
    }
};
struct Scene {
    std::array<Axis, 3> axes;
    QVector<Line> lines;
    QVector<Label> labels;
    QVector<Scale> scales;
};
struct Frame {
    std::array<Axis, 3> axes;
    QVector<QLineF> lines;
    QVector<ProjectedLabel> labels;
    QVector<Scale> scales;
};

inline QPointF project(const SurfaceCamera &camera, const QSizeF &size, const QVector3D &world)
{
    // GUI 的左上原点；GPU 后端的 clipSpaceCorrMatrix 与纹理翻转由 QRhi / Qt Quick 处理。
    return Projection(camera, size).point(world);
}

inline std::array<Axis, 3> placeAxes(const Projection &projection)
{
    const QVector3D lower(-kSurfaceSize.x() / 2, -kSurfaceSize.y() / 2, 0);
    const QVector3D upper(kSurfaceSize.x() / 2, kSurfaceSize.y() / 2, kSurfaceSize.z()), center = (lower + upper) / 2;
    std::array<Axis, 3> axes;
    for (int dimension = 0; dimension < 3; ++dimension) {
        auto &axis = axes[dimension];
        QVector3D along;
        along[dimension] = 1;
        // 法线必须垂直于轴；视线扣掉轴向分量后，就是在此约束下最朝向相机的法线。
        const auto normal = projection.towardCamera - along * QVector3D::dotProduct(projection.towardCamera, along);
        axis.hasTextPlane = normal.lengthSquared() > 1e-12f;
        if (axis.hasTextPlane) {
            const auto facing = normal.normalized();
            axis.down = QVector3D::crossProduct(along, facing).normalized();
            // 三轴同一选侧规则：投影外侧优先向下，水平方向则取左侧。
            // 文字朝下向量就是离轴方向，字顶因此始终靠轴；阅读方向由正面约束确定。
            const QPointF down = projection.vector(axis.down);
            const double tolerance = QLineF(QPointF(), down).length() * 0.00001;
            const double side = std::abs(down.y()) > tolerance ? down.y() : -down.x();
            if (side < 0) axis.down = -axis.down;
            axis.right = QVector3D::crossProduct(facing, axis.down).normalized();
        }
        // 枚举平行于本轴的四条边，按投影外侧选轮廓边；重合时取靠近相机的一条。
        const QPointF outward = projection.vector(axis.down);
        double bestSide = -std::numeric_limits<double>::infinity();
        float bestDepth = -std::numeric_limits<float>::infinity();
        for (int corner = 0; corner < 4; ++corner) {
            QVector3D start = lower;
            int bit = 0;
            for (int other = 0; other < 3; ++other)
                if (other != dimension) start[other] = corner & (1 << bit++) ? upper[other] : lower[other];
            auto end = start;
            end[dimension] = upper[dimension];
            const QVector3D offset = (start + end) / 2 - center;
            const double side = QPointF::dotProduct(projection.vector(offset), outward);
            const float depth = QVector3D::dotProduct(offset, projection.towardCamera);
            if (side > bestSide + 0.000001 || (std::abs(side - bestSide) <= 0.000001 && depth > bestDepth)) {
                bestSide = side;
                bestDepth = depth;
                axis.start = start;
                axis.end = end;
            }
        }
    }
    return axes;
}

inline QVector<double> ticks(double low, double high, int target)
{
    QVector<double> values{low};
    if (!(high > low)) return values;
    const double rough = (high - low) / std::max(2, target);
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double normalized = rough / magnitude;
    const double step = magnitude * (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10);
    const double first = std::ceil(low / step);
    for (int i = 0; i < 16; ++i) {
        const double value = (first + i) * step;
        if (value >= high) break;
        if (value > low) values.append(value);
    }
    values.append(high); // 末档是下标上界，例如 199，不把维度 200 画成最后一格。
    return values;
}

inline Scene makeScene(const Projection &projection, SurfaceGrid grid, double zMin, double zMax, const QFont &font)
{
    Scene scene;
    scene.axes = placeAxes(projection);
    scene.scales = {{QStringLiteral("频点 (下标)"), 0, static_cast<double>(grid.columns - 1), {}},
                    {QStringLiteral("距离门 (下标)"), 0, static_cast<double>(grid.rows - 1), {}},
                    {QStringLiteral("功率谱值"), zMin, zMax, {}}};
    const QFontMetricsF metrics(font);
    QVector<Label> endpoints, interiors, titles;
    for (int axis = 0; axis < 3; ++axis) {
        const auto &placement = scene.axes[axis];
        scene.lines.append({placement.start, placement.end});
        const double length = QLineF(projection.point(placement.start), projection.point(placement.end)).length();
        auto &scale = scene.scales[axis];
        scale.ticks = ticks(scale.low, scale.high, std::clamp(static_cast<int>(length / 50), 3, 6));
        // 视线沿轴或轴投影不足一个像素时没有可用的文字姿态。
        if (!placement.hasTextPlane || length < 1) continue;
        const QVector3D right = placement.right * kTextWorldUnit;
        const QVector3D down = placement.down * kTextWorldUnit;
        const auto label = [&](const QString &text, const QVector3D &anchor, double offset, bool title) {
            const QSizeF textSize(metrics.horizontalAdvance(text) + 6, metrics.height() + 2);
            return Label{text, textSize, anchor + down * static_cast<float>(offset + textSize.height() / 2),
                         right, down, axis, title};
        };
        double tickOutreach = 0;
        for (qsizetype i = 0; i < scale.ticks.size(); ++i) {
            const double value = scale.ticks[i];
            const float fraction = static_cast<float>((value - scale.low) / (scale.high - scale.low));
            const QVector3D anchor = placement.start + (placement.end - placement.start) * fraction;
            scene.lines.append({anchor, anchor + down * 5});
            const auto text = label(QString::number(value, 'g', 12), anchor, 12, false);
            tickOutreach = std::max(tickOutreach, text.size.height());
            (i == 0 || i + 1 == scale.ticks.size() ? endpoints : interiors).append(text);
        }
        titles.append(label(scale.title, (placement.start + placement.end) / 2, 18 + tickOutreach, true));
    }
    // 三轴采用相同优先级：轴名、范围端点、中间刻度。
    scene.labels = titles + endpoints + interiors;
    return scene;
}

inline Frame projectScene(const Scene &scene, const Projection &projection)
{
    Frame frame{scene.axes, {}, {}, scene.scales};
    for (const auto &line : scene.lines) frame.lines.append(QLineF(projection.point(line.start), projection.point(line.end)));
    const QRectF viewport(QPointF(3, 3), projection.size - QSizeF(6, 6));
    for (const auto &object : scene.labels) {
        // 正交相机下平面投影为仿射变换；字宽保留轴向压缩，不把它归一化成屏幕文字。
        const QPointF right = projection.vector(object.right), down = projection.vector(object.down), center = projection.point(object.center);
        // 真正接近沿轴观察时，单字会压成细线；按投影字宽判可读性，不按相机角度分支。
        const double glyphWidth = QLineF(QPointF(), right).length() * object.size.width() / static_cast<double>(object.text.size());
        if (glyphWidth < 2) continue;
        ProjectedLabel candidate{object, QTransform(right.x(), right.y(), down.x(), down.y(), center.x(), center.y())};
        if (!viewport.contains(candidate.footprint().boundingRect())) continue;
        // 两侧各承担一半留白，避免结果依赖文字的加入顺序与投影压缩方向。
        const auto padded = candidate.footprint(1.5);
        const bool overlaps = std::any_of(frame.labels.begin(), frame.labels.end(),
            [&](const ProjectedLabel &placed) { return padded.intersects(placed.footprint(1.5)); });
        if (!overlaps) frame.labels.append(candidate);
    }
    return frame;
}

inline Frame layout(const SurfaceCamera &camera, const QSizeF &size, SurfaceGrid grid,
                    double zMin, double zMax, const QFont &font)
{
    if (size.width() < 1 || size.height() < 1 || grid.rows < 2 || grid.columns < 2) return {};
    const Projection projection(camera, size);
    return projectScene(makeScene(projection, grid, zMin, zMax, font), projection);
}
}

class SurfaceAxesItem final : public QQuickPaintedItem
{
public:
    explicit SurfaceAxesItem(QQuickItem *parent) : QQuickPaintedItem(parent)
    {
        setObjectName(QStringLiteral("surfaceAxes"));
        setAcceptedMouseButtons(Qt::NoButton);
        setZ(1);
        font_ = QGuiApplication::font();
        font_.setPixelSize(13);
        connect(this, &QQuickItem::paletteChanged, this, &SurfaceAxesItem::syncPalette);
        syncPalette();
    }
    void setView(const SurfaceCamera &camera, const PreparedSurface &surface, const QSizeF &size)
    {
        if (camera_.view() == camera.view() && camera_.zoom == camera.zoom && grid_ == surface.grid
            && low_ == surface.zMin && high_ == surface.zMax && size == this->size()) return;
        camera_ = camera;
        grid_ = surface.grid;
        low_ = surface.zMin;
        high_ = surface.zMax;
        setSize(size);
        frame_ = SurfaceAxes::layout(camera_, size, grid_, low_, high_, font_);
        update();
    }
    const SurfaceAxes::Frame &frame() const { return frame_; }
    void paint(QPainter *painter) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);
        painter->setFont(font_);
        painter->setPen(QPen(foreground_, 1.2));
        for (const auto &line : frame_.lines) painter->drawLine(line);
        for (const auto &label : frame_.labels) {
            painter->save();
            painter->setWorldTransform(label.transform, true);
            painter->fillRect(label.object.localBounds(), background_);
            painter->drawText(label.object.localBounds(), Qt::AlignCenter, label.object.text);
            painter->restore();
        }
    }
private:
    void syncPalette()
    {
        auto *palette = qvariant_cast<QObject *>(property("palette"));
        foreground_ = palette->property("text").value<QColor>();
        background_ = palette->property("base").value<QColor>();
        background_.setAlpha(230);
        update();
    }
    SurfaceCamera camera_;
    SurfaceGrid grid_;
    double low_ = 0, high_ = 0;
    QFont font_;
    QColor foreground_, background_;
    SurfaceAxes::Frame frame_;
};
