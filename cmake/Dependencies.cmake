include_guard(GLOBAL)
include(FetchContent)
# 消费方已提供目标时直接复用；否则拉取经过验证的固定提交，不启用 Handle 可选模块。
if(NOT TARGET Lidar::Algorithm)
    FetchContent_Declare(lidar_algorithm
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/Algorithm.git
        GIT_TAG 3161796b7c5fa9231f0c16f094e281f3535336b4)
    FetchContent_MakeAvailable(lidar_algorithm)
endif()
if(NOT TARGET Lidar::UICore)
    FetchContent_Declare(lidar_uicore
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/UICore.git
        GIT_TAG ac88c0c9fd037f2e2a62c62d033b05cf1814adfa)
    FetchContent_MakeAvailable(lidar_uicore)
endif()
