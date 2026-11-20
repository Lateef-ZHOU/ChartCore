include_guard(GLOBAL)
include(FetchContent)
# 只加载族的数据契约；Algorithm 由 FamilyModels 传递，不加载族运行层。
if(NOT TARGET Lidar::UICore)
    FetchContent_Declare(lidar_uicore
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/UICore.git
        GIT_TAG ac88c0c9fd037f2e2a62c62d033b05cf1814adfa)
    FetchContent_MakeAvailable(lidar_uicore)
endif()

if(NOT TARGET Lidar::FamilyModels)
    FetchContent_Declare(lidar_familymodels
        GIT_REPOSITORY https://github.com/Lateef-ZHOU/TaskCore.git
        GIT_TAG 9de615c13ec99831e61933a303fcbd83f059cfba
        SOURCE_SUBDIR models)
    FetchContent_MakeAvailable(lidar_familymodels)
endif()
