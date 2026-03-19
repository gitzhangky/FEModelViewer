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

# ── Qt 路径提示（可通过命令行覆盖：-DCMAKE_PREFIX_PATH=...）──
# 优先使用用户传入的 CMAKE_PREFIX_PATH，否则尝试常见安装位置
if(NOT CMAKE_PREFIX_PATH)
    if(WIN32)
        # Windows：根据编译器选择对应的 Qt 套件
        if(MSVC)
            set(_qt_kits
                "C:/Qt/5.15.2/msvc2019_64"
                "C:/Qt/5.15.2/msvc2017_64"
                "C:/Qt/5.13.2/msvc2017_64"
                "D:/Qt/5.15.2/msvc2019_64"
                "D:/Qt/5.15.2/msvc2017_64"
                "D:/Qt/5.13.2/msvc2017_64"
            )
        else()
            set(_qt_kits
                "C:/Qt/5.15.2/mingw81_64"
                "C:/Qt/5.13.2/mingw73_64"
                "D:/Qt/5.15.2/mingw81_64"
                "D:/Qt/5.13.2/mingw73_64"
            )
        endif()
        foreach(_qt_hint IN LISTS _qt_kits)
            if(EXISTS "${_qt_hint}")
                set(CMAKE_PREFIX_PATH "${_qt_hint}")
                break()
            endif()
        endforeach()
    elseif(APPLE)
        # macOS：Homebrew（Apple Silicon 和 Intel）
        foreach(_qt_hint
            "/opt/homebrew/opt/qt@5"
            "/usr/local/opt/qt@5"
        )
            if(EXISTS "${_qt_hint}")
                set(CMAKE_PREFIX_PATH "${_qt_hint}")
                break()
            endif()
        endforeach()
    endif()
endif()

# ── 依赖库查找 ──

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
find_package(OpenGL REQUIRED)

# MSVC：源文件以 UTF-8 编码解析，避免中文字符串报语法错误
if(MSVC)
    add_compile_options(/utf-8)
endif()

# macOS：抑制 OpenGL 弃用警告（macOS 10.14+ 将 OpenGL 标为 deprecated）
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

# GLM：header-only，使用 FetchContent 自动下载并添加子目录
include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
)
FetchContent_MakeAvailable(glm)

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
    PartsPanel.cpp
)

# macOS：打包为 .app bundle
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "FEModelViewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glm::glm
    Qt5::Core Qt5::Gui Qt5::Widgets
    Qt5::OpenGL
)
