cmake_minimum_required(VERSION 3.20)

project(CuteCat VERSION 1.0.1 LANGUAGES CXX)

# 技术栈严格按《技术栈方案.md》：Qt 6 + C++17 + CMake + rlottie（可选）+ QJson
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    enable_language(RC)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)

set(CUTECAT_SOURCES
    app.rc
    src/main.cpp
    src/pet.h
    src/pet.cpp
    src/config.h
    src/config.cpp
    src/animation/pet_skin.h
    src/animation/image_skin.h
    src/animation/image_skin.cpp
    src/animation/lottie_skin.h
    src/animation/lottie_skin.cpp
    src/animation/skin_factory.h
    src/animation/skin_factory.cpp
)

qt_add_executable(CuteCat WIN32 ${CUTECAT_SOURCES})

target_include_directories(CuteCat PRIVATE src)
target_link_libraries(CuteCat PRIVATE Qt6::Widgets)
target_compile_definitions(CuteCat PRIVATE CUTECAT_VERSION="${PROJECT_VERSION}")

# 产物统一放到 build/bin/<Config>/CuteCat.exe（多配置生成器会自动加配置子目录）
set_target_properties(CuteCat PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 内置资源：默认配置 + 默认形象（qrc:/pet/cat_idle.png）
qt_add_resources(CuteCat "cutecat_resources"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/config.json
        resources/pet/cat_idle.png
)

# rlottie 是可选依赖：默认关闭，桌宠使用内置图片皮肤；
# 打开后可用 config.json 里 appearance.animation = "lottie" 播放 Lottie 动画。
option(CUTECAT_WITH_RLOTTIE "启用 rlottie Lottie 动画皮肤（需要先编译 rlottie）" OFF)
if(CUTECAT_WITH_RLOTTIE)
    find_package(rlottie CONFIG REQUIRED)
    target_link_libraries(CuteCat PRIVATE rlottie::rlottie)
    target_compile_definitions(CuteCat PRIVATE CUTECAT_HAS_RLOTTIE=1)
endif()

if(MSVC)
    target_compile_options(CuteCat PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(CuteCat PRIVATE -Wall -Wextra)
endif()