#pragma once
#include <QVariantMap>
#include <QtQml/qqmlinfo.h>
#include <QtQml/qqml.h>
#include <QtQml/qqmlregistration.h>

#include "SurfaceState.h"
#include "SurfaceAxes.h"
#include "SurfaceData.h"
#include <QColor>
#include <QFile>
#include <QMouseEvent>
#include <QQuickRhiItem>
#include <QQuickWindow>
#include <QVariant>
#include <QWheelEvent>
#include <rhi/qrhi.h>
#include <array>
#include <memory>
#include <limits>

namespace SurfaceRenderDetail {
// 固定超采样策略：同时过滤曲面轮廓与片元网线，不提供运行时模式选择。
inline constexpr int kSupersampleScale = 2;
}

// 专用高度面：接收原始 double 列表和 Z 范围，按固定行列补零、钳值并生成显示快照。
// 相机、坐标与 GPU 资源属于显示层；窗口是否可见由 QML 控制层决定，不在此监听业务源。
class SurfaceItem : public QQuickRhiItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration REQUIRED)
public:
    explicit SurfaceItem(QQuickItem *parent = nullptr)
        : QQuickRhiItem(parent), axes_(new SurfaceAxesItem(this))
    {
        connect(this, &QQuickItem::widthChanged, this, &SurfaceItem::syncAxes);
        connect(this, &QQuickItem::heightChanged, this, &SurfaceItem::syncAxes);
        setAcceptedMouseButtons(Qt::LeftButton);
        setSmooth(true); // Qt 合成时线性缩小超采样纹理。
        syncTargetSize();
        connect(this, &QQuickItem::paletteChanged, this, &SurfaceItem::syncPalette);
        syncPalette();
    }
    // 创建期配置：QML 必填，组件完成后固定。
    QVariantMap configuration() const
    {
        return {{"rows", grid_.rows},
                {"columns", grid_.columns}};
    }
    void setConfiguration(const QVariantMap &value)
    {
        if (isComponentComplete())
        {
            qmlWarning(this) << "Chart configuration is creation-only.";
            return;
        }
        const SurfaceGrid grid{value.value("rows").toInt(), value.value("columns").toInt()};
        if (grid.rows < 2 || grid.columns < 2 || qint64(grid.rows) * grid.columns > std::numeric_limits<int>::max() / 6)
            qFatal("Invalid surface grid.");
        grid_ = grid;
    }

    Q_INVOKABLE bool setData(const QVector<double> &values, double low, double high)
    {
        auto next = prepareSurface(values, grid_, low, high);
        if (!next) return false; // 非有限值不进入 GPU，也不破坏上次有效显示。
        next->revision = surface_.revision + 1;
        surface_ = std::move(*next);
        syncAxes();
        update();
        return true;
    }
    const PreparedSurface &surface() const { return surface_; }
    const SurfaceCamera &camera() const { return camera_; }
    QColor background() const { return background_; }
    QColor foreground() const { return foreground_; }
    std::shared_ptr<SurfaceRenderStats> stats() const { return stats_; }
    Q_INVOKABLE void resetCamera() { camera_ = {}; updateView(); }

protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void geometryChange(const QRectF &geometry, const QRectF &oldGeometry) override
    {
        QQuickRhiItem::geometryChange(geometry, oldGeometry);
        if (geometry.size() != oldGeometry.size())
            syncTargetSize();
    }
    void itemChange(ItemChange change, const ItemChangeData &data) override
    {
        QQuickRhiItem::itemChange(change, data);
        if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged)
            syncTargetSize();
    }
    void mousePressEvent(QMouseEvent *event) override { lastMouse_ = event->position(); event->accept(); }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        const QPointF delta = event->position() - lastMouse_;
        lastMouse_ = event->position();
        camera_.rotate(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
        updateView();
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override { event->accept(); }
    void wheelEvent(QWheelEvent *event) override
    {
        camera_.scale(static_cast<float>(event->angleDelta().y()) / 120.0f);
        updateView();
        event->accept();
    }

private:
    void syncAxes() { axes_->setView(camera_, surface_, size()); }
    void updateView() { syncAxes(); update(); }
    void syncTargetSize()
    {
        // 固定缓冲尺寸使用物理像素；尺寸、宿主窗口或屏幕 DPR 改变时由 Qt 通知重算。
        const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
        setFixedColorBufferWidth(SurfaceRenderDetail::kSupersampleScale * std::max(1, qRound(width() * dpr)));
        setFixedColorBufferHeight(SurfaceRenderDetail::kSupersampleScale * std::max(1, qRound(height() * dpr)));
    }
    void syncPalette()
    {
        auto *palette = qvariant_cast<QObject *>(property("palette"));
        background_ = palette->property("base").value<QColor>();
        foreground_ = palette->property("text").value<QColor>();
        update();
    }
    SurfaceGrid grid_{2, 2};
    SurfaceAxesItem *const axes_;
    PreparedSurface surface_;
    SurfaceCamera camera_;
    QColor background_, foreground_;
    QPointF lastMouse_;
    const std::shared_ptr<SurfaceRenderStats> stats_ = std::make_shared<SurfaceRenderStats>();
};

namespace SurfaceRenderDetail {
inline QShader loadShader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot open shader: %s", qPrintable(path));
    const QShader shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid())
        qFatal("Invalid shader: %s", qPrintable(path));
    return shader;
}

class SurfaceRenderer final : public QQuickRhiItemRenderer
{
public:
    explicit SurfaceRenderer(std::shared_ptr<SurfaceRenderStats> stats) : stats_(std::move(stats))
    { ++stats_->created; }
    ~SurfaceRenderer() override { ++stats_->destroyed; }

protected:
    void initialize(QRhiCommandBuffer *) override
    {
        if (rhi_ != rhi()) {
            pipeline_.reset();
            pass_.reset();
            bindings_.reset();
            uniform_.reset();
            heights_.reset();
            positions_.reset();
            indices_.reset();
            rhi_ = rhi();
            uploaded_ = 0;
            grid_ = {0, 0};
            stats_->backend = static_cast<int>(rhi_->backend());
            stats_->timestampsSupported = rhi_->isFeatureSupported(QRhi::Timestamps);

        }
        if (!uniform_) {
            uniform_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 112));
            if (!uniform_->create())
                qFatal("Surface buffer creation failed");
            bindings_.reset(rhi_->newShaderResourceBindings());
            bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniform_.get())});
            if (!bindings_->create())
                qFatal("Surface bindings creation failed");
        }
        const auto *targetPass = renderTarget()->renderPassDescriptor();
        if (pipeline_ && (!pass_->isCompatible(targetPass) || samples_ != renderTarget()->sampleCount()))
            pipeline_.reset();
        if (!pipeline_) {
            pass_.reset(targetPass->newCompatibleRenderPassDescriptor());
            samples_ = renderTarget()->sampleCount();
            pipeline_.reset(rhi_->newGraphicsPipeline());
            pipeline_->setShaderStages({
                {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/surface.vert.qsb"))},
                {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/surface.frag.qsb"))}
            });
            QRhiVertexInputLayout layout;
            layout.setBindings({{sizeof(float)}, {2 * sizeof(float)}});
            layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float, 0}, {1, 1, QRhiVertexInputAttribute::Float2, 0}});
            pipeline_->setVertexInputLayout(layout);
            pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
            pipeline_->setCullMode(QRhiGraphicsPipeline::None);
            pipeline_->setDepthTest(true);
            pipeline_->setDepthWrite(true);
            pipeline_->setSampleCount(samples_);
            pipeline_->setShaderResourceBindings(bindings_.get());
            pipeline_->setRenderPassDescriptor(pass_.get());
            if (!pipeline_->create())
                qFatal("Surface pipeline creation failed");
        }
    }

    void synchronize(QQuickRhiItem *item) override
    {
        const qint64 start = stats_->measuring.load() ? surfaceNowNs() : 0;
        const auto *surfaceItem = static_cast<SurfaceItem *>(item);
        surface_ = surfaceItem->surface();
        camera_ = surfaceItem->camera();
        background_ = surfaceItem->background();
        foreground_ = surfaceItem->foreground();
        outputSize_ = surfaceItem->size() * surfaceItem->window()->effectiveDevicePixelRatio();
        syncNs_ = start ? surfaceNowNs() - start : 0;
    }

    void render(QRhiCommandBuffer *commands) override
    {
        const bool measuring = stats_->measuring.load();
        const qint64 start = measuring ? surfaceNowNs() : 0;
        SurfaceTiming timing;
        timing.syncNs = syncNs_;
        // QRhi 返回延迟的整窗 GPU 时间，不能当作本曲面独占时间或当前帧耗时。
        if (measuring && stats_->timestampsSupported.load())
            timing.delayedWindowGpuMs = commands->lastCompletedGpuTime() * 1000.0;
        auto *updates = rhi_->nextResourceUpdateBatch();
        const bool hasSurface = !surface_.heights.isEmpty() && surface_.heights.size() == surface_.grid.pointCount();
        if (hasSurface && grid_ != surface_.grid) {
            grid_ = surface_.grid;
            indexCount_ = (grid_.rows - 1) * (grid_.columns - 1) * 6;
            heights_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, grid_.pointCount() * sizeof(float)));
            positions_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, grid_.pointCount() * 2 * sizeof(float)));
            indices_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, indexCount_ * sizeof(quint32)));
            if (!heights_->create() || !positions_->create() || !indices_->create())
                qFatal("Surface grid buffer creation failed");
            QVector<float> xy;
            QVector<quint32> indices;
            xy.reserve(grid_.pointCount() * 2);
            indices.reserve(indexCount_);
            for (int row = 0; row < grid_.rows; ++row) {
                for (int col = 0; col < grid_.columns; ++col) {
                    xy.append(2.0f * col / (grid_.columns - 1) - 1.0f);
                    xy.append(2.0f * row / (grid_.rows - 1) - 1.0f);
                    if (row + 1 < grid_.rows && col + 1 < grid_.columns) {
                        const auto a = static_cast<quint32>(row * grid_.columns + col);
                        const auto b = a + grid_.columns;
                        indices.append({a, a + 1, b, a + 1, b + 1, b});
                    }
                }
            }
            updates->uploadStaticBuffer(positions_.get(), xy.constData());
            updates->uploadStaticBuffer(indices_.get(), indices.constData());
            uploaded_ = 0;
        }
        if (hasSurface && uploaded_ != surface_.revision) {
            const qint64 uploadStart = measuring ? surfaceNowNs() : 0;
            const int bytes = grid_.pointCount() * sizeof(float);
            updates->updateDynamicBuffer(heights_.get(), 0, bytes, surface_.heights.constData());
            timing.uploadEnqueueNs = measuring ? surfaceNowNs() - uploadStart : 0;
            uploaded_ = surface_.revision;
            ++stats_->uploads;
            stats_->uploadedBytes += bytes;
        }
        const QSize size = renderTarget()->pixelSize();
        const QMatrix4x4 matrix = rhi_->clipSpaceCorrMatrix()
            * camera_.projection(static_cast<float>(outputSize_.width() / outputSize_.height())) * camera_.view();
        std::array<float, 28> uniforms{};
        std::copy_n(matrix.constData(), 16, uniforms.begin());
        uniforms[16] = kSurfaceSize.x();
        uniforms[17] = kSurfaceSize.y();
        uniforms[18] = kSurfaceSize.z();
        const auto step = surfaceMeshStep(grid_, kSurfaceSize);
        uniforms[20] = (grid_.columns - 1) / step.x();
        uniforms[21] = (grid_.rows - 1) / step.y();
        // 导数来自高分辨率目标，换回最终物理像素后再决定线宽和过密淡出。
        // 使用实际附件尺寸，包含 Qt 的最小纹理尺寸与像素取整。
        uniforms[22] = static_cast<float>(size.width() / outputSize_.width());
        uniforms[23] = static_cast<float>(size.height() / outputSize_.height());
        uniforms[24] = foreground_.redF();
        uniforms[25] = foreground_.greenF();
        uniforms[26] = foreground_.blueF();
        uniforms[27] = foreground_.alphaF();
        updates->updateDynamicBuffer(uniform_.get(), 0, sizeof(uniforms), uniforms.data());
        commands->beginPass(renderTarget(), background_, {1.0f, 0}, updates);
        if (hasSurface) {
            commands->setGraphicsPipeline(pipeline_.get());
            commands->setViewport({0.0f, 0.0f, static_cast<float>(size.width()), static_cast<float>(size.height())});
            commands->setShaderResources(bindings_.get());
            const std::array<QRhiCommandBuffer::VertexInput, 2> input{{{heights_.get(), 0}, {positions_.get(), 0}}};
            commands->setVertexInput(0, static_cast<int>(input.size()), input.data(), indices_.get(), 0, QRhiCommandBuffer::IndexUInt32);
            commands->drawIndexed(indexCount_);
            stats_->revision = surface_.revision;
        }
        commands->endPass();
        ++stats_->draws;
        if (measuring) {
            timing.recordNs = surfaceNowNs() - start;
            stats_->recordTiming(timing);
        }
        // 不自激连续绘制；新数据、相机、尺寸或场景恢复由 Item/Qt 请求下一帧。
    }

private:
    SurfaceGrid grid_{0, 0};
    int indexCount_ = 0;
    qint64 syncNs_ = 0;
    const std::shared_ptr<SurfaceRenderStats> stats_;
    QRhi *rhi_ = nullptr;
    std::unique_ptr<QRhiBuffer> positions_, heights_, indices_, uniform_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiRenderPassDescriptor> pass_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    PreparedSurface surface_;
    SurfaceCamera camera_;
    QColor background_, foreground_;
    QSizeF outputSize_;
    quint64 uploaded_ = 0;
    int samples_ = 0;
};
}

inline QQuickRhiItemRenderer *SurfaceItem::createRenderer() { return new SurfaceRenderDetail::SurfaceRenderer(stats_); }
