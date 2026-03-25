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

# ── RENDER 模块（可独立安装的共享库）──

set(RENDER_HEADERS
    Camera.h
    Geometry.h
    GLWidget.h
    FEModel.h
    FEField.h
    FEMeshConverter.h
    FENode.h
    FEElement.h
    FEGroup.h
    FERenderData.h
    FEResultData.h
    FEPickResult.h
)

set(RENDER_SOURCES
    Camera.cpp
    Geometry.cpp
    GLWidget.cpp
    FEModel.cpp
    FEField.cpp
    FEMeshConverter.cpp
)

add_library(FERender SHARED ${RENDER_SOURCES} ${RENDER_HEADERS})

include(GenerateExportHeader)
generate_export_header(FERender
    EXPORT_MACRO_NAME FERENDER_EXPORT
    EXPORT_FILE_NAME  ${CMAKE_CURRENT_BINARY_DIR}/ferender_export.h
)

target_include_directories(FERender
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include/FERender>
)

target_link_libraries(FERender PUBLIC
    OpenGL::GL
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL
)

# GLM 是 header-only 库，直接添加 include 路径即可（避免 export set 问题）
target_include_directories(FERender SYSTEM PUBLIC
    $<BUILD_INTERFACE:${glm_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# ── GUI 模块（应用程序）──

# 手动 MOC 应用程序自己的头文件，避免 AUTOMOC 重复 moc FERender 的 Q_OBJECT 类
qt5_wrap_cpp(VIEWER_MOC_SRCS
    MainWindow.h
    FEModelPanel.h
    PartsPanel.h
    ResultPanel.h
    PickPanel.h
    ControlPanel.h
    MonitorPanel.h
)

add_executable(${PROJECT_NAME}
    main.cpp
    MainWindow.cpp
    FEModelPanel.cpp
    PartsPanel.cpp
    ResultPanel.cpp
    PickPanel.cpp
    ControlPanel.cpp
    MonitorPanel.cpp
    ${VIEWER_MOC_SRCS}
)

set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC OFF)

# macOS：打包为 .app bundle
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "FEModelViewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE FERender)

# ── 安装规则 ──

include(GNUInstallDirs)

# 安装库文件
install(TARGETS FERender
    EXPORT FERenderTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装可执行文件
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装头文件
install(FILES ${RENDER_HEADERS}
    "${CMAKE_CURRENT_BINARY_DIR}/ferender_export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/FERender
)

# 安装 GLM 头文件（header-only，随库一起分发）
install(DIRECTORY ${glm_SOURCE_DIR}/glm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# 导出 CMake Target，其他项目可以 find_package(FERender) 使用
install(EXPORT FERenderTargets
    FILE FERenderTargets.cmake
    NAMESPACE FERender::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FERender
)

# 生成 FERenderConfig.cmake
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FERenderConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FERenderConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FERender
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/FERenderConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FERender
)
