#pragma once
#include <chrono>
#include <QMetaType>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "Algorithm/Geometry/GimbalModel.h"
#include "Algorithm/Wind/VerticalWindProfile.h"

// Qt 消费侧登记原类型；算法与设备核心不依赖 QML。
using RxTime = std::chrono::system_clock::time_point;
struct RxTimeQml
{
    Q_GADGET
    QML_FOREIGN(RxTime)
    QML_ANONYMOUS
};

// QML 只转交整段历史；保留 RxTime 与 WorldModel，不展开成 JS 数组或数值时间。
struct WorldPoseHistoryQml
{
    Q_GADGET
    QML_FOREIGN(WorldPoseHistory)
    QML_ANONYMOUS
};

// Qt 界面消费侧的值适配；不增加数据成员。
// QML 持有姿态值，读取角度和换算目标都不需要回访设备。
class WorldModelQml : private WorldModel
{
    Q_GADGET
    QML_FOREIGN(WorldModel)
    QML_VALUE_TYPE(worldModel)
    QML_EXTENDED(WorldModelQml)
    Q_PROPERTY(double azimuth READ azimuth CONSTANT)
    Q_PROPERTY(double elevation READ elevation CONSTANT)
    Q_PROPERTY(double roll READ roll CONSTANT)
public:
    using WorldModel::azimuth;
    using WorldModel::elevation;
    using WorldModel::roll;

    Q_INVOKABLE QVariantMap gimbalAngles(double azimuth, double elevation) const
    {
        const auto pose = PanTiltModel::fromOrientation(
            toOrientation().inverse() * WorldModel(azimuth, elevation).toOrientation());
        return {{"pan", pose.pan()}, {"tilt", pose.tilt()}};
    }
};

// QML 只转交整帧份额，不展开层数组，也不转换时间。
struct VerticalWindProfileQml
{
    Q_GADGET
    QML_FOREIGN(VerticalWindProfilePtr)
    QML_ANONYMOUS
};
