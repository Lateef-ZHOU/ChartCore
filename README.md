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

## 来源与许可

从 [LiDAR_DataACQ](https://github.com/Lateef-ZHOU/LiDAR_DataACQ) 的模块分离提交 `94ad6f8` 提取；初始分仓保持运行逻辑，独立整理构建、验证与发布入口。

自有代码采用 [MIT](LICENSE)，Copyright (c) 2026 周涛 (ZHOU TAO)。第三方依赖不随本仓库改为 MIT，参见 [依赖说明](THIRD_PARTY_NOTICES.md)。
