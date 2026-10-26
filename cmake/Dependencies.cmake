include_guard(GLOBAL)
include(FetchContent)
# 消费方已提供目标时直接复用；否则拉取经过验证的固定提交，不启用 Handle 可选模块。
if(NOT TARGET Lidar::Algorithm)
    FetchContent_Declare(lidar_algorithm
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/Algorithm.git
        GIT_TAG 00244abaf8fdabb6ad6bf2d2b875af4e60522a4f)
    FetchContent_MakeAvailable(lidar_algorithm)
endif()
if(NOT TARGET Lidar::UICore)
    FetchContent_Declare(lidar_uicore
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/UICore.git
        GIT_TAG bf646946b884e362ee4bd10225ebb1ff16bdcd63)
    FetchContent_MakeAvailable(lidar_uicore)
endif()
