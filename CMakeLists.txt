cmake_minimum_required(VERSION 3.28)
project(LidarChartCore LANGUAGES CXX)
include(cmake/Dependencies.cmake)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.11.2 REQUIRED COMPONENTS Quick Qml GuiPrivate ShaderTools)
if(NOT TARGET Lidar::Algorithm OR NOT TARGET Lidar::UICore)
    message(FATAL_ERROR "ChartCore requires Lidar::Algorithm and Lidar::UICore")
endif()
get_target_property(ui_output Lidar::UICore QT_QML_MODULE_OUTPUT_DIRECTORY)
get_filename_component(ui_import_root "${ui_output}/../.." ABSOLUTE)
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE views CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "include/*.qml")
foreach(view IN LISTS views)
    get_filename_component(name "${view}" NAME)
    set_source_files_properties("${view}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()
qt_add_qml_module(LidarChartCore STATIC URI Lidar.Charts VERSION 1.0
    CLASS_NAME LidarChartsPlugin RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Lidar/Charts"
    DEPENDENCIES Lidar.UI
    IMPORT_PATH "${ui_import_root}"
    SOURCES ${headers} QML_FILES ${views})
add_library(Lidar::ChartCore ALIAS LidarChartCore)
target_include_directories(LidarChartCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/UI/Chart"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UI/Chart/Base"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UI/Chart/Surface")
target_compile_features(LidarChartCore PUBLIC cxx_std_20)
target_link_libraries(LidarChartCore PUBLIC Lidar::Algorithm Lidar::UICore Qt6::Quick Qt6::GuiPrivate)
target_compile_definitions(LidarChartCore PUBLIC QT_NO_KEYWORDS NOMINMAX)
set(surface "${CMAKE_CURRENT_SOURCE_DIR}/include/UI/Chart/Surface")
qt_add_shaders(LidarChartCore surface_shaders PREFIX /shaders BASE "${surface}/shaders"
    FILES "${surface}/shaders/surface.vert" "${surface}/shaders/surface.frag")

if(MSVC)
    target_compile_options(LidarChartCore PRIVATE /W4)
    get_target_property(module_sources LidarChartCore SOURCES)
    foreach(source IN LISTS module_sources)
        if(source MATCHES "/[.]rcc/qmlcache/")
            set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS "/wd4702")
        endif()
    endforeach()
endif()

# 作为依赖时不接管宿主的测试开关。
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
