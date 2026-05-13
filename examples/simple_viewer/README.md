# FERender Simple Viewer

这个示例是一个独立 Qt 程序，演示外部项目如何调用安装后的 `FERender` 渲染引擎。

它刻意不使用源码树里的头文件路径，而是通过：

```cmake
find_package(FERender REQUIRED CONFIG)
target_link_libraries(simple_viewer PRIVATE FERender::FERender)
```

## 构建 FERender 并安装

在仓库根目录执行：

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build -j 4
cmake --install build
```

## 构建示例

```bash
cmake -S examples/simple_viewer -B build/examples/simple_viewer \
  -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build build/examples/simple_viewer -j 4
```

运行：

```bash
./build/examples/simple_viewer/simple_viewer
```

## 调用流程

示例代码展示了最小调用链：

1. 用 `FEModel` 构造一个简单的 HEX8 有限元模型。
2. 调用 `FEMeshConverter::toRenderData(model)` 得到 `FERenderData`。
3. 把 `FERenderData::mesh` 传给 `GLWidget::setMesh()`。
4. 把拾取映射表传给 `setTriangleToElementMap()`、`setVertexToNodeMap()`、`setTriangleToPartMap()` 和 `setEdgeToPartMap()`。
5. 调用 `fitToModel()` 自动适配视角。

关键代码在 `main.cpp` 的 `loadSampleModel()` 中。
