include_guard(GLOBAL)
include(FetchContent)
# 消费方已提供目标时直接复用；否则拉取经过验证的固定提交，不启用 Handle 可选模块。
if(NOT TARGET Lidar::Algorithm)
    FetchContent_Declare(lidar_algorithm
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/Algorithm.git
        GIT_TAG ce55feae3ef405e1cd85ad97d24eb946dd49a82d)
    FetchContent_MakeAvailable(lidar_algorithm)
endif()
if(NOT TARGET Lidar::UICore)
    FetchContent_Declare(lidar_uicore
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/UICore.git
        GIT_TAG a305f0a16fc237db42da1e417582ea9650aa6e6f)
    FetchContent_MakeAvailable(lidar_uicore)
endif()
