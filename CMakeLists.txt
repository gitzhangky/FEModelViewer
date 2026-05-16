# ============================================================
# CMake 构建配置文件
# 项目名称：FEModelViewer
# 描述：基于 Qt5 + OpenGL 的有限元模型查看器
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(FEModelViewer LANGUAGES C CXX)

# 用户未显式指定 -DCMAKE_INSTALL_PREFIX 时，默认装到 build 目录下的 install/，
# 而不是 Windows 上需要管理员权限的 C:/Program Files (x86)/。
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

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
    FEParser.h
    FEResultMapper.h
    FEResultRepository.h
    FEDeformation.h
    FEAnimationController.h
    FEProbe.h
    FEPostFilter.h
    FEIsoSurface.h
    Theme.h
)

set(RENDER_SOURCES
    Camera.cpp
    ColorBarOverlay.cpp
    Geometry.cpp
    GLWidget.cpp
    FEModel.cpp
    FEField.cpp
    FEMeshConverter.cpp
    FEParser_inp.cpp
    FEParser_bdf.cpp
    FEParser_op2.cpp
    FEParser_op2results.cpp
    FEParser_unv.cpp
    FEResultMapper.cpp
    FEResultRepository.cpp
    FEDeformation.cpp
    FEAnimationController.cpp
    FEProbe.cpp
    FEPostFilter.cpp
    FEIsoSurface.cpp
)

# Shader 资源文件
qt5_add_resources(RENDER_QRC shaders.qrc)

add_library(FERender SHARED ${RENDER_SOURCES} ${RENDER_HEADERS} ${RENDER_QRC})

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
    ControlPanel.h
    MonitorPanel.h
)

add_executable(${PROJECT_NAME}
    main.cpp
    MainWindow.cpp
    FEModelPanel.cpp
    PartsPanel.cpp
    ResultPanel.cpp
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

# ── 测试 ──

include(CTest)

if(BUILD_TESTING)
    add_executable(test_result_mapper
        tests/test_result_mapper.cpp
    )
    target_link_libraries(test_result_mapper PRIVATE FERender)
    add_test(NAME result_mapper
        COMMAND test_result_mapper
    )

    add_executable(test_result_repository
        tests/test_result_repository.cpp
    )
    target_link_libraries(test_result_repository PRIVATE FERender)
    add_test(NAME result_repository
        COMMAND test_result_repository
    )

    add_executable(test_deformation
        tests/test_deformation.cpp
    )
    target_link_libraries(test_deformation PRIVATE FERender)
    add_test(NAME deformation
        COMMAND test_deformation
    )

    add_executable(test_probe
        tests/test_probe.cpp
    )
    target_link_libraries(test_probe PRIVATE FERender)
    add_test(NAME probe
        COMMAND test_probe
    )

    add_executable(test_post_filter
        tests/test_post_filter.cpp
    )
    target_link_libraries(test_post_filter PRIVATE FERender)
    add_test(NAME post_filter
        COMMAND test_post_filter
    )

    add_executable(test_post_state
        tests/test_post_state.cpp
    )
    target_link_libraries(test_post_state PRIVATE FERender)
    add_test(NAME post_state
        COMMAND test_post_state
    )

    add_executable(test_import_path_state
        tests/test_import_path_state.cpp
    )
    target_include_directories(test_import_path_state PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(test_import_path_state PRIVATE Qt5::Core)
    add_test(NAME import_path_state
        COMMAND test_import_path_state
    )

    add_executable(test_iso_cube_data
        tests/test_iso_cube_data.cpp
    )
    target_link_libraries(test_iso_cube_data PRIVATE FERender)
    add_test(NAME iso_cube_data
        COMMAND test_iso_cube_data
    )
    set_tests_properties(iso_cube_data PROPERTIES
        ENVIRONMENT "FERENDER_TEST_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples/data"
    )

    add_executable(test_threshold_row_data
        tests/test_threshold_row_data.cpp
    )
    target_link_libraries(test_threshold_row_data PRIVATE FERender)
    add_test(NAME threshold_row_data
        COMMAND test_threshold_row_data
    )
    set_tests_properties(threshold_row_data PROPERTIES
        ENVIRONMENT "FERENDER_TEST_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples/data"
    )

    qt5_wrap_cpp(TEST_RESULT_PANEL_MOC_SRCS ResultPanel.h)
    add_executable(test_result_panel
        tests/test_result_panel.cpp
        ResultPanel.cpp
        ${TEST_RESULT_PANEL_MOC_SRCS}
    )
    set_target_properties(test_result_panel PROPERTIES AUTOMOC OFF)
    target_link_libraries(test_result_panel PRIVATE FERender Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME result_panel
        COMMAND test_result_panel
    )

    qt5_wrap_cpp(TEST_PARTS_PANEL_MOC_SRCS PartsPanel.h)
    add_executable(test_parts_panel
        tests/test_parts_panel.cpp
        PartsPanel.cpp
        ${TEST_PARTS_PANEL_MOC_SRCS}
    )
    set_target_properties(test_parts_panel PROPERTIES AUTOMOC OFF)
    target_link_libraries(test_parts_panel PRIVATE FERender Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME parts_panel
        COMMAND test_parts_panel
    )

    add_test(NAME ferender_public_headers
        COMMAND ${CMAKE_COMMAND}
            "-DINSTALLED_HEADERS=${RENDER_HEADERS}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_public_headers.cmake"
    )

    add_test(NAME fe_model_viewer_parse_missing_file_is_headless
        COMMAND ${CMAKE_COMMAND}
            "-DVIEWER=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DMISSING_FILE=${CMAKE_CURRENT_BINARY_DIR}/missing.op2"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_parse_missing_file_test.cmake"
    )

    add_test(NAME cli_parse_mode_layering
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_parse_mode_layering.cmake"
    )

    add_test(NAME qopenglwidget_framebuffer_restore
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/GLWidget.cpp"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_qopenglwidget_framebuffer.cmake"
    )

    add_test(NAME installed_simple_viewer_example
        COMMAND ${CMAKE_COMMAND}
            "-DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DPROJECT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DINSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/example-install"
            "-DEXAMPLE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/examples/simple_viewer"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_installed_example.cmake"
    )
endif()
