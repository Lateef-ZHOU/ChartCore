# ChartCore

Qt Quick 二维图表与 QRhi 三维高度面，以及配套 QML 图窗。Windows / Linux，C++20，CMake ≥ 3.28。

## 边界与依赖

- Qt ≥ 6.11.2；Qt 由使用环境提供，不在仓库中打包。
- 上游：[Algorithm](https://github.com/Lateef-ZHOU/Algorithm), [UICore](https://github.com/Lateef-ZHOU/UICore)。已有同名 CMake 目标时复用；否则 FetchContent 拉取固定完整提交。
- 本仓库不依赖 LiDAR_DataACQ 源码、应用目录或设备型号；不包含第三方二进制。
- 使用 Qt Quick/QML、GuiPrivate/QRhi 和 ShaderTools；必须使用同一套 Qt 开发包/运行库，升级 Qt 后重新构建和验证。
- Surface 固定宽高各 2 倍 SSAA，完整 XY 域、Z 钳值、无 Roll 旋转及滚轮缩放。QML 持原谱，绘制器仅持显示快照；二维图自行保留历史。
- 图形后端由应用选择；Windows 可使用 Direct3D11，Linux 可使用 Qt RHI 支持的后端。离屏测试验证装配，不代表硬件 GPU 画质验证。

## 独立构建和验证

安装对应 Qt 开发包，然后执行（将路径替换为本机 Qt 前缀）：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.2/platform -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

Windows 在安装 MSVC 的环境运行以上命令，Qt 选择对应 MSVC 构建；Linux 使用 GCC 与 OpenMP 开发环境。
Linux 还需 Qt Quick 运行依赖；ChartCore 需 GuiPrivate 头和 ShaderTools/qsb。GitHub Actions 使用 Qt 6.11.2，执行构建、QML 检查和无窗口组件装配。

## 接入应用

```cmake
include(FetchContent)
FetchContent_Declare(lidar_chartcore
    GIT_REPOSITORY https://github.com/Lateef-ZHOU/ChartCore.git
    GIT_TAG <经过验证的完整提交号>)
FetchContent_MakeAvailable(lidar_chartcore)
target_link_libraries(my_app PRIVATE Lidar::ChartCore)
```

从仓库提交列表选择固定版本；示例占位值必须替换，不使用浮动分支保证可复现。多个模块共用已有上游目标，应用若需覆盖上游版本应先统一声明并创建它们。标准 `FETCHCONTENT_SOURCE_DIR_LIDAR_模块名`（模块名大写无分隔）可用于本地开发，发布验证应取消覆盖并实际拉取远端。

界面是静态 QML 插件；应用还须链接 `LidarUICoreplugin LidarChartCoreplugin`，并在一个 C++ 源文件包含 `<QtQml/QQmlExtensionPlugin>`，分别写 `Q_IMPORT_QML_PLUGIN(LidarUIPlugin)`、`Q_IMPORT_QML_PLUGIN(LidarChartsPlugin)`。QML 使用 `import Lidar.UI` 和 `import Lidar.Charts`。完整最小示例见 tests/smoke.cpp。

构建工具/部署工具的 QML 导入根目录，由目标的 `QT_QML_MODULE_OUTPUT_DIRECTORY` 向上两级取得；不要假定上游仓库与本仓库相邻。

## 激光器三组趋势图（2026-10-28 用户定）

`LaserTelemetryChart.qml` 暂作为专用图种放在 ChartCore，纵向排列三幅图：温度（激光器、MCU）、电流（PA1、PA2、PA3）、电压（系统电压）。复用 SeriesChart 的折线、图例、抽稀和手势；AngleChart 与风速/CNR 图保持各自接口，暂不合并成通用多图框架。后续是否统一 Plot 接口仍待定。

```qml
import Lidar.Charts
LaserTelemetryChart {
    id: trends
    anchors.fill: parent
    configuration: ({ windowSeconds: 120, capacity: 132,
        temperature: { minimum: 0, maximum: 80 },
        current:     { minimum: 0, maximum: 10 },
        voltage:     { minimum: 20, maximum: 30 } })
}
```

配置仅在创建时交入：时间窗单位秒，capacity 是**每条曲线**最多保留的点数；三个纵轴范围分别为 °C、A、V。示例范围只是演示值，不代表设备额定范围、限值或报警条件，接入官邸时应按使用需求给定。曲线保存原数值，超出显示范围的部分裁剪显示；不钳制或改写测量值。

官邸将回复解析为本次测量值后调用 `trends.append(at, values)`：

| values 字段 | 曲线 | 单位 |
| --- | --- | --- |
| laserTemperature / mcuTemperature | 激光器 / MCU 温度 | °C |
| pa1Current / pa2Current / pa3Current | PA1 / PA2 / PA3 电流 | A |
| voltage | 系统电压 | V |

`at` 原样转交 C++ 的 RxTime，只有绘图器转为横轴秒数。2026-10-28 用户进一步裁定：values **必须包含全部六个数值字段**，六条曲线在同一时刻同时追加，容量裁剪也保持行对齐。官邸按协议包更新自己的最新值缓存，未回报项沿用缓存、从未回报项传 NaN；图表既不凑值也不维护第二份最新值。缺字段的输入在分发任何面板之前整次拒绝，NaN 仍占据本行并显示为断点，不伪造零值。悬停显示统一的快照时间和六值；它表示当时缓存状态，不声明六项都在该包重新测量。容量按点数裁剪，不因墙钟跳变成批清空。

官邸在一包解析结束后最多推送一次：一包六项是一次通知，六个独立包则是六次全量通知/36 个曲线点；不新增跨包等待、节流或更新掩码。后续设备轮询优先在一次读取请求中查询六项，减少实际分包机会。普通状态/确认不驱动图表。

三图同步时间轴的跟随、平移、缩放、复位与悬停；纵轴分别调整。裸滚轮缩放当前图两轴，Shift+滚轮只缩放公共时间轴，Ctrl+滚轮只缩放当前纵轴，拖动平移，双击复位当前纵轴并恢复公共时间轴跟随。隐藏期间继续接收历史，恢复后直接绘制。

一个图窗只表示一个观察对象；官邸按回复所属模块路由数据，不按当前单选按钮错误归类。切换对象可明确调用 `clear()` 清空历史/悬停（保留视口），或分别装配种子/放大模块图窗。图表不持设备、不认识寄存器、不做轮询，也不会因空白、旧值或收到数据而发出任何查询。模块路由、响应式查询及激光器 QML 官邸接线仍由设备仓库后续实现。

独立演示：构建后运行 `build/tests/laser_telemetry --preview`（Windows 为 `build/tests/Release/laser_telemetry.exe --preview`，需 Qt bin 在 PATH）。演示源只在测试程序中，每秒生成六项数据，不连接设备；`--capture image.png` 保存实际渲染画面。常规 CTest 使用离屏模式，检查缺项整次拒绝、NaN 占位、完整快照的六曲线行对齐、容量、单位分组、QML 转交 RxTime、横轴手势联动、独立纵轴、隐藏恢复及 8 秒运行；原 Surface 冒烟继续执行。

2026-10-28 全量更新验证：Windows / MSVC 19.51 / Qt 6.11.2 与 Linux / GCC 16.2.1 / Qt 6.12.0，Release 构建和 public_headers 独立单头编译通过，编译器零警告；CTest 各 2/2，通过 77 项趋势图检查及原角度图、风速/CNR、Surface 装配。两端 CMake qmllint 目标通过，新图无诊断；既有文件仍有 unused-import 提示，Qt 6.12 还提示 Surface 的 state 命名遮蔽。测试故意提交缺项输入时的拒绝诊断符合预期，Windows 离屏字体扫描仍有既有 FreeType 提示。首版曾检查独立 D3D11 / 4× MSAA 实际渲染布局；本次检查数据对齐与交互，没有重做 GPU 画质比较。未连接激光器，未作硬件测量验证；本轮未更新应用固定依赖或推送仓库。

本轮证据：Windows build/build-full-snapshot.log、build/test-full-snapshot.log、build/lint-full-snapshot.log；Linux ~/.cache/chart-telemetry-20261028/ 下 build-full-snapshot-final.log、test-full-snapshot-final.log、lint-full-snapshot-final.log。

## 来源与许可

从 [LiDAR_DataACQ](https://github.com/Lateef-ZHOU/LiDAR_DataACQ) 的模块分离提交 `94ad6f8` 提取；初始分仓保持运行逻辑，独立整理构建、验证与发布入口。

自有代码采用 [MIT](LICENSE)，Copyright (c) 2026 周涛 (ZHOU TAO)。第三方依赖不随本仓库改为 MIT，参见 [依赖说明](THIRD_PARTY_NOTICES.md)。
