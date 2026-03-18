# ============================================================
# CMake 构建配置文件
# 项目名称：FEModelViewer
# 描述：基于 Qt5 + OpenGL 的有限元模型查看器
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(FEModelViewer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用 Qt 的自动 MOC
set(CMAKE_AUTOMOC ON)

# ── 依赖库查找 ──

set(CMAKE_PREFIX_PATH /opt/homebrew/opt/qt@5)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
find_package(glm REQUIRED HINTS /opt/homebrew/Cellar/glm/1.0.3/share/glm)
find_package(OpenGL REQUIRED)

# ── 构建目标 ──

add_executable(${PROJECT_NAME}
    main.cpp
    Camera.cpp
    Geometry.cpp
    GLWidget.cpp
    MonitorPanel.cpp
    MainWindow.cpp
    FEModel.cpp
    FEField.cpp
    FEMeshConverter.cpp
    FEModelPanel.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glm::glm
    Qt5::Core Qt5::Gui Qt5::Widgets
    Qt5::OpenGL
)
