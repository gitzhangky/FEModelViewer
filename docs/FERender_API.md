# FERender 渲染库 API 参考文档

> **版本**：1.0
> **依赖**：Qt 5.x (Core, Gui, Widgets, OpenGL) · OpenGL 4.1+ · GLM 0.9.9+
> **语言标准**：C++17
> **构建产物**：共享库 `FERender.dll` / `libFERender.so` / `libFERender.dylib`

---

## 目录

- [1. 快速开始](#1-快速开始)
  - [1.1 CMake 集成](#11-cmake-集成)
  - [1.2 最小示例](#12-最小示例)
  - [1.3 典型工作流](#13-典型工作流)
- [2. 数据层 API](#2-数据层-api)
  - [2.1 FENode — 有限元节点](#21-fenode--有限元节点)
  - [2.2 FEElement — 有限元单元](#22-feelement--有限元单元)
  - [2.3 ElementType — 单元类型枚举](#23-elementtype--单元类型枚举)
  - [2.4 FEGroup — 分组结构](#24-fegroup--分组结构)
  - [2.5 FEModel — 有限元模型容器](#25-femodel--有限元模型容器)
  - [2.6 FEField — 结果场与色谱](#26-ffield--结果场与色谱)
  - [2.7 FEResultData — 多工况结果层级](#27-feresultdata--多工况结果层级)
- [3. 转换层 API](#3-转换层-api)
  - [3.1 Mesh — 三角网格数据结构](#31-mesh--三角网格数据结构)
  - [3.2 Geometry — 基础几何体生成器](#32-geometry--基础几何体生成器)
  - [3.3 FERenderData — 渲染数据包](#33-ferenderdata--渲染数据包)
  - [3.4 FEMeshConverter — 网格转换器](#34-femeshconverter--网格转换器)
- [4. 渲染层 API](#4-渲染层-api)
  - [4.1 Camera — 轨道相机](#41-camera--轨道相机)
  - [4.2 GLWidget — OpenGL 渲染窗口](#42-glwidget--opengl-渲染窗口)
- [5. 交互层 API](#5-交互层-api)
  - [5.1 PickMode — 拾取模式](#51-pickmode--拾取模式)
  - [5.2 FEPickResult — 拾取结果](#52-fpickresult--拾取结果)
  - [5.3 FESelection — 选中状态](#53-feselection--选中状态)
- [6. 完整使用示例](#6-完整使用示例)
  - [6.1 加载模型并渲染](#61-加载模型并渲染)
  - [6.2 显示标量云图](#62-显示标量云图)
  - [6.3 部件可见性控制](#63-部件可见性控制)
  - [6.4 拾取交互](#64-拾取交互)
  - [6.5 变形显示](#65-变形显示)
- [附录 A：头文件清单](#附录-a头文件清单)
- [附录 B：单元类型速查表](#附录-b单元类型速查表)

---

## 1. 快速开始

### 1.1 CMake 集成

将 FERender 安装到某个前缀路径后，在消费项目的 `CMakeLists.txt` 中：

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# 指向 FERender 的安装路径
list(APPEND CMAKE_PREFIX_PATH "/path/to/ferender/install")

find_package(FERender REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE FERender::FERender)
```

`FERender::FERender` 会自动传递以下依赖：
- Qt5::Core, Qt5::Gui, Qt5::Widgets, Qt5::OpenGL
- OpenGL::GL
- GLM 头文件路径

### 1.2 最小示例

```cpp
#include <QApplication>
#include <QSurfaceFormat>
#include "GLWidget.h"
#include "FEModel.h"
#include "FEMeshConverter.h"

int main(int argc, char* argv[]) {
    // 配置 OpenGL
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(32);
    fmt.setSamples(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // 构建一个简单模型
    FEModel model;
    model.addNode(1, {0.0f, 0.0f, 0.0f});
    model.addNode(2, {1.0f, 0.0f, 0.0f});
    model.addNode(3, {0.5f, 1.0f, 0.0f});
    model.addElement(1, ElementType::TRI3, {1, 2, 3});

    // 转换为渲染数据
    FERenderData rd = FEMeshConverter::toRenderData(model);

    // 创建渲染窗口并显示
    GLWidget viewer;
    viewer.setMesh(rd.mesh);
    viewer.setTriangleToElementMap(rd.triangleToElement);
    viewer.setVertexToNodeMap(rd.vertexToNode);
    viewer.fitToModel(model.computeCenter(), model.computeSize());
    viewer.resize(800, 600);
    viewer.show();

    return app.exec();
}
```

### 1.3 典型工作流

```
                ┌─────────────┐
                │  数据加载    │  用户自行解析 FEM 文件
                │  填充 FEModel│  (Nastran/Abaqus/OP2...)
                └──────┬──────┘
                       ↓
              ┌────────────────┐
              │ FEMeshConverter │  模型 → 渲染数据
              │ ::toRenderData()│
              └───────┬────────┘
                      ↓
           ┌──────────────────┐
           │   FERenderData   │  Mesh + 映射表
           └────────┬─────────┘
                    ↓
         ┌─────────────────────┐
         │     GLWidget        │  设置 Mesh → 渲染
         │  setMesh / 映射表   │  鼠标交互 → 拾取
         └─────────────────────┘
```

---

## 2. 数据层 API

### 2.1 FENode — 有限元节点

**头文件**：`FENode.h`

```cpp
struct FENode {
    int       id;      // 节点全局编号（不要求连续）
    glm::vec3 coords;  // 节点坐标 (x, y, z)，单精度
};
```

| 字段     | 类型        | 说明 |
|----------|-------------|------|
| `id`     | `int`       | 节点全局唯一 ID，支持不连续编号 |
| `coords` | `glm::vec3` | 三维坐标，默认 `(0, 0, 0)` |

> **注意**：使用单精度 `float`，满足可视化精度需求。如需双精度计算，需在外部使用 `glm::dvec3`。

---

### 2.2 FEElement — 有限元单元

**头文件**：`FEElement.h`

```cpp
struct FEElement {
    int              id;       // 单元全局编号
    ElementType      type;     // 单元类型
    std::vector<int> nodeIds;  // 节点 ID 列表（顺序遵循标准约定）
};
```

| 字段      | 类型               | 说明 |
|-----------|--------------------|------|
| `id`      | `int`              | 单元全局唯一 ID |
| `type`    | `ElementType`      | 单元类型枚举 |
| `nodeIds` | `std::vector<int>` | 构成该单元的节点 ID，顺序遵循 Abaqus/Nastran 约定 |

**辅助函数**：

```cpp
// 获取单元维度：1(线) / 2(壳) / 3(实体)
int elementDimension(ElementType type);

// 获取角节点数（不含高阶中间节点）
int elementCornerNodeCount(ElementType type);
```

---

### 2.3 ElementType — 单元类型枚举

**头文件**：`FEElement.h`

```cpp
enum class ElementType {
    // 1D 线单元
    BAR2,        // 2 节点杆单元
    BAR3,        // 3 节点二次杆单元

    // 2D 壳/板单元
    TRI3,        // 3 节点三角形
    TRI6,        // 6 节点二次三角形
    QUAD4,       // 4 节点四边形
    QUAD8,       // 8 节点二次四边形

    // 3D 实体单元
    TET4,        // 4 节点四面体
    TET10,       // 10 节点二次四面体
    HEX8,        // 8 节点六面体
    HEX20,       // 20 节点二次六面体
    WEDGE6,      // 6 节点三棱柱
    PYRAMID5,    // 5 节点四棱锥
};
```

完整速查表见 [附录 B](#附录-b单元类型速查表)。

---

### 2.4 FEGroup — 分组结构

**头文件**：`FEGroup.h`

#### FEPart（部件）

```cpp
struct FEPart {
    std::string      name;        // 部件名称
    std::vector<int> nodeIds;     // 属于该部件的节点 ID
    std::vector<int> elementIds;  // 属于该部件的单元 ID
    bool             visible;     // 是否可见，默认 true
};
```

#### FENodeSet（节点集）

```cpp
struct FENodeSet {
    std::string      name;     // 集合名称（如 "FixedSupport"）
    std::vector<int> nodeIds;  // 节点 ID 列表
};
```

#### FEElementSet（单元集）

```cpp
struct FEElementSet {
    std::string      name;        // 集合名称（如 "Steel_Part"）
    std::vector<int> elementIds;  // 单元 ID 列表
};
```

---

### 2.5 FEModel — 有限元模型容器

**头文件**：`FEModel.h`

FEModel 是整个有限元数据的统一入口，持有模型的全部信息。**纯数据层，不包含任何渲染逻辑。**

#### 公开数据成员

| 成员 | 类型 | 说明 |
|------|------|------|
| `name` | `std::string` | 模型名称（通常来自文件名） |
| `filePath` | `std::string` | 源文件路径 |
| `nodes` | `std::unordered_map<int, FENode>` | 节点表：ID → 节点 |
| `elements` | `std::unordered_map<int, FEElement>` | 单元表：ID → 单元 |
| `parts` | `std::vector<FEPart>` | 部件列表 |
| `nodeSets` | `std::vector<FENodeSet>` | 节点集列表 |
| `elementSets` | `std::vector<FEElementSet>` | 单元集列表 |
| `scalarFields` | `std::vector<FEScalarField>` | 标量结果场列表 |
| `vectorFields` | `std::vector<FEVectorField>` | 矢量结果场列表 |

#### 公开方法

```cpp
// ── 添加数据 ──

// 添加节点，若 ID 已存在则覆盖
void addNode(int id, const glm::vec3& coords);

// 添加单元
void addElement(int id, ElementType type, const std::vector<int>& nodeIds);

// ── 查询 ──

// 按 ID 查找节点坐标，未找到返回 nullptr
const glm::vec3* nodeCoords(int id) const;

// 节点总数
int nodeCount() const;

// 单元总数
int elementCount() const;

// 模型是否为空
bool isEmpty() const;

// ── 空间信息 ──

// 计算轴对齐包围盒 (AABB)
void computeBoundingBox(glm::vec3& bbMin, glm::vec3& bbMax) const;

// 计算几何中心（包围盒中点）
glm::vec3 computeCenter() const;

// 计算最大尺寸（包围盒对角线长度）
float computeSize() const;

// ── 管理 ──

// 清空所有数据（节点、单元、分组、结果场）
void clear();
```

#### 使用示例

```cpp
FEModel model;
model.name = "BracketAssembly";

// 添加节点
model.addNode(1, {0.0f, 0.0f, 0.0f});
model.addNode(2, {1.0f, 0.0f, 0.0f});
model.addNode(3, {1.0f, 1.0f, 0.0f});
model.addNode(4, {0.0f, 1.0f, 0.0f});

// 添加四边形单元
model.addElement(1, ElementType::QUAD4, {1, 2, 3, 4});

// 添加部件
FEPart part;
part.name = "Bracket";
part.elementIds = {1};
part.nodeIds = {1, 2, 3, 4};
model.parts.push_back(part);

// 查询
auto* pos = model.nodeCoords(2);   // → (1, 0, 0)
float size = model.computeSize();  // 包围盒对角线长度
```

---

### 2.6 FEField — 结果场与色谱

**头文件**：`FEField.h`

#### FieldLocation（场数据位置）

```cpp
enum class FieldLocation {
    Node,      // 定义在节点上（位移、温度等）
    Element    // 定义在单元上（应力、应变等）
};
```

#### FEScalarField（标量场）

```cpp
struct FEScalarField {
    std::string                    name;      // 场名称（如 "Von Mises Stress"）
    std::string                    unit;      // 单位（如 "MPa"）
    FieldLocation                  location;  // 数据位置，默认 Node
    std::unordered_map<int, float> values;    // ID → 标量值

    // 计算值域范围
    void computeRange(float& minVal, float& maxVal) const;
};
```

#### FEVectorField（矢量场）

```cpp
struct FEVectorField {
    std::string                        name;      // 场名称（如 "Displacement"）
    std::string                        unit;      // 单位（如 "mm"）
    FieldLocation                      location;  // 数据位置，默认 Node
    std::unordered_map<int, glm::vec3> values;    // ID → 矢量值

    // 计算矢量幅值（模长）范围
    void computeMagnitudeRange(float& minMag, float& maxMag) const;
};
```

#### ColorMapType（色谱类型）

```cpp
enum class ColorMapType {
    Rainbow,    // 经典彩虹：蓝 → 青 → 绿 → 黄 → 红
    Jet,        // Jet 色谱（类 MATLAB 默认）
    CoolWarm,   // 冷暖色谱：蓝 → 白 → 红（适合正负值对比）
    Grayscale,  // 灰度：黑 → 白
    Viridis     // Viridis（感知均匀，色盲友好）
};
```

#### ColorMap（色谱映射器）

```cpp
struct ColorMap {
    ColorMapType type;            // 色谱类型，默认 Rainbow
    int          discreteLevels;  // 分段色阶数，默认 10（0 = 平滑渐变）

    // 归一化值 [0,1] → RGB 颜色 [0,1]
    glm::vec3 map(float t) const;

    // 原始值 → RGB（自动归一化）
    glm::vec3 map(float value, float minVal, float maxVal) const;
};
```

#### 使用示例

```cpp
// 创建温度标量场
FEScalarField tempField;
tempField.name = "Temperature";
tempField.unit = "°C";
tempField.location = FieldLocation::Node;
tempField.values[1] = 25.0f;
tempField.values[2] = 100.0f;
tempField.values[3] = 75.0f;

// 查询值域
float minT, maxT;
tempField.computeRange(minT, maxT);  // minT=25, maxT=100

// 色谱映射
ColorMap cmap;
cmap.type = ColorMapType::Jet;
cmap.discreteLevels = 12;            // 12 级色阶

glm::vec3 color = cmap.map(75.0f, minT, maxT);  // → 对应的 RGB
```

---

### 2.7 FEResultData — 多工况结果层级

**头文件**：`FEResultData.h`

用于组织 OP2 等求解结果文件中的层级数据。

```
FEResultData                    顶层容器
  └─ FESubcase                  一个工况
       └─ FEResultType          一种结果类型（位移/应力）
            └─ FEResultComponent 单个分量（X/Y/Z/Magnitude...）
```

#### FEResultComponent

```cpp
struct FEResultComponent {
    std::string   name;    // 分量名称（"X", "Y", "Z", "Magnitude", "Von Mises"）
    FEScalarField field;   // 标量场数据
};
```

#### FEResultType

```cpp
struct FEResultType {
    std::string                    name;         // 类型名称（"Displacement", "Stress"）
    std::vector<FEResultComponent> components;   // 分量列表
    FEVectorField                  vectorField;  // 可选矢量场
    bool                           hasVector;    // 是否有矢量场，默认 false
};
```

#### FESubcase

```cpp
struct FESubcase {
    int                        id;           // 工况 ID
    std::string                name;         // 工况名称
    std::vector<FEResultType>  resultTypes;  // 结果类型列表
};
```

#### FEResultData

```cpp
struct FEResultData {
    std::vector<FESubcase> subcases;   // 工况列表

    bool empty() const;   // 是否有数据
    void clear();         // 清空所有结果
};
```

#### 使用示例

```cpp
FEResultData results;

FESubcase sc;
sc.id = 1;
sc.name = "Static Load Case 1";

FEResultType dispType;
dispType.name = "Displacement";
dispType.hasVector = true;
dispType.vectorField.name = "Displacement";
dispType.vectorField.unit = "mm";
dispType.vectorField.values[1] = {0.1f, 0.0f, -0.05f};
dispType.vectorField.values[2] = {0.3f, 0.0f, -0.12f};

// 添加幅值分量
FEResultComponent magComp;
magComp.name = "Magnitude";
magComp.field.name = "Displacement Magnitude";
magComp.field.unit = "mm";
magComp.field.values[1] = 0.112f;
magComp.field.values[2] = 0.323f;
dispType.components.push_back(magComp);

sc.resultTypes.push_back(dispType);
results.subcases.push_back(sc);
```

---

## 3. 转换层 API

### 3.1 Mesh — 三角网格数据结构

**头文件**：`Geometry.h`

```cpp
struct Mesh {
    // ── 主要渲染数据 ──
    std::vector<float>        vertices;     // 顶点数据（交错存储）
    std::vector<unsigned int> indices;      // 三角形索引（每 3 个一组）

    // ── 边线数据 ──
    std::vector<float>        edgeVertices; // 边线顶点（仅位置，GL_LINES 用）
    std::vector<unsigned int> edgeIndices;  // 边线索引（每 2 个一组）

    // ── 单元完整边线（用于选中高亮）──
    std::vector<float>              elemEdgeVertices;   // 边顶点坐标
    std::vector<int>                elemEdgeToElement;  // 边 → 单元 ID
    std::vector<std::pair<int,int>> elemEdgeNodeIds;    // 边的节点 ID 对（已排序）

    // ── 便捷方法 ──
    void addVertex(glm::vec3 pos, glm::vec3 normal);
    void addTriangle(unsigned int a, unsigned int b, unsigned int c);
    void addFlatTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c);   // 自动计算面法线
    void addFlatQuad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d);
};
```

**顶点内存布局**（每顶点 6 个 float）：

```
偏移  0: px, py, pz   ← 位置
偏移 12: nx, ny, nz   ← 法线
──────────────────────
步长 = 6 × sizeof(float) = 24 bytes
```

| 方法 | 说明 |
|------|------|
| `addVertex(pos, normal)` | 追加一个顶点（位置 + 法线） |
| `addTriangle(a, b, c)` | 追加一个三角形索引 |
| `addFlatTriangle(a, b, c)` | 追加三角形并自动计算面法线（flat shading） |
| `addFlatQuad(a, b, c, d)` | 追加四边形，拆分为 2 个三角形 |

---

### 3.2 Geometry — 基础几何体生成器

**头文件**：`Geometry.h`

命名空间 `Geometry` 提供 7 种基础几何体生成函数，均返回以原点为中心的标准大小 `Mesh`。

```cpp
namespace Geometry {
    Mesh cube();                                          // 正方体
    Mesh tetrahedron();                                   // 正四面体
    Mesh triangularPrism();                               // 三棱柱
    Mesh cylinder(int segments = 36);                     // 圆柱
    Mesh cone(int segments = 36);                         // 圆锥
    Mesh sphere(int rings = 24, int sectors = 36);        // 球体
    Mesh torus(int ringSegs = 36, int tubeSegs = 24);     // 圆环
}
```

| 函数 | 参数 | 默认值 | 说明 |
|------|------|--------|------|
| `cube()` | 无 | — | 单位正方体 |
| `tetrahedron()` | 无 | — | 正四面体 |
| `triangularPrism()` | 无 | — | 三棱柱 |
| `cylinder(segments)` | 侧面分段数 | 36 | 圆柱体 |
| `cone(segments)` | 侧面分段数 | 36 | 圆锥体 |
| `sphere(rings, sectors)` | 纬线数、经线数 | 24, 36 | 球体 |
| `torus(ringSegs, tubeSegs)` | 环分段、管分段 | 36, 24 | 圆环体 |

#### 使用示例

```cpp
GLWidget viewer;
Mesh sphereMesh = Geometry::sphere(32, 64);  // 更精细的球体
viewer.setMesh(sphereMesh);
```

---

### 3.3 FERenderData — 渲染数据包

**头文件**：`FERenderData.h`

`FERenderData` 是 `FEMeshConverter` 的输出结构，捆绑了可渲染的三角网格和反向映射表。

```cpp
struct FERenderData {
    Mesh             mesh;                // 三角网格
    std::vector<int> triangleToElement;   // 三角形索引 → FEM 单元 ID
    std::vector<int> triangleToFace;      // 三角形索引 → 单元内面序号
    std::vector<int> vertexToNode;        // 渲染顶点索引 → FEM 节点 ID
    std::vector<int> triangleToPart;      // 三角形索引 → 部件索引（-1=无部件）
    std::vector<int> edgeToPart;          // 边线索引 → 部件索引（-1=无部件）

    // 便捷查询方法
    int elementAtTriangle(int triIndex) const;  // 三角形 → 单元 ID
    int faceAtTriangle(int triIndex) const;     // 三角形 → 面序号
    int nodeAtVertex(int vertIndex) const;      // 顶点 → 节点 ID
    int triangleCount() const;                   // 三角形总数
    int vertexCount() const;                     // 顶点总数
    void clear();                                // 清空所有数据
};
```

**映射关系示意**：

```
渲染三角形 #5  ─── triangleToElement[5] ───→  FEM 单元 #102
                ─── triangleToFace[5]    ───→  面序号 2（单元的第 3 个面）
                ─── triangleToPart[5]    ───→  部件索引 0

渲染顶点 #10   ─── vertexToNode[10]     ───→  FEM 节点 #57
```

> **注意**：flat shading 下同一个 FEM 节点可能对应多个渲染顶点（因不同面法线不同而复制），所以 `vertexToNode` 是多对一映射。

---

### 3.4 FEMeshConverter — 网格转换器

**头文件**：`FEMeshConverter.h`

纯静态工具类，将 FEModel 转换为 GLWidget 可渲染的数据。**无状态，所有方法为 `static`**。

#### 进度回调

```cpp
using ProgressCallback = std::function<void(int percent)>;  // percent: 0~100
```

#### 主要接口（返回 FERenderData = Mesh + 映射表）

```cpp
// 整个模型 → 渲染数据包
// 自动处理 2D 三角化 + 3D 外表面提取
static FERenderData toRenderData(
    const FEModel& model,
    const ProgressCallback& progress = nullptr
);

// 指定单元子集 → 渲染数据包
// 用于分部件显示、选中高亮等
static FERenderData toRenderData(
    const FEModel& model,
    const std::vector<int>& elementIds,
    const ProgressCallback& progress = nullptr
);

// 带云图颜色的渲染数据包
// Mesh 顶点格式变为 [pos(3) + normal(3) + color(3)]
static FERenderData toColoredRenderData(
    const FEModel& model,
    const FEScalarField& field,
    const ColorMap& colorMap,
    float minVal, float maxVal
);
```

#### 辅助接口（仅返回 Mesh，无映射表）

```cpp
// 变形后的网格：新坐标 = 原坐标 + displacement × scale
static Mesh toDeformedMesh(
    const FEModel& model,
    const FEVectorField& displacement,
    float scale = 1.0f
);

// 线框网格（仅边，GL_LINES 渲染）
static Mesh toWireframeMesh(const FEModel& model);
```

#### 内部转换逻辑

| 单元维度 | 处理方式 |
|----------|----------|
| 1D (BAR2/BAR3) | 暂跳过（后续可生成管状几何） |
| 2D (TRI3/QUAD4...) | 直接三角化 |
| 3D (TET4/HEX8...) | 提取外表面 → 三角化 |

**外表面提取算法**：遍历所有单元的所有面，将面的节点 ID 排序后作为 key，只保留被单个单元引用的面（即外表面）。

#### 使用示例

```cpp
FEModel model;
// ... 填充模型数据 ...

// 带进度回调的转换
FERenderData rd = FEMeshConverter::toRenderData(model, [](int pct) {
    qDebug() << "Converting:" << pct << "%";
});

// 仅转换某些单元
std::vector<int> subset = {1, 2, 5, 8};
FERenderData partRd = FEMeshConverter::toRenderData(model, subset);

// 变形显示
FEVectorField disp;
disp.values[1] = {0.1f, 0.0f, -0.05f};
// ...
Mesh deformed = FEMeshConverter::toDeformedMesh(model, disp, 10.0f);  // 10 倍放大
```

---

## 4. 渲染层 API

### 4.1 Camera — 轨道相机

**头文件**：`Camera.h`

围绕目标点旋转的轨道相机，使用球坐标系（yaw / pitch / distance）。

#### 公开数据成员

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `yaw` | `float` | `0.0` | 水平旋转角（绕 Y 轴，度） |
| `pitch` | `float` | `0.0` | 垂直旋转角（仰俯角，度） |
| `distance` | `float` | `3.0` | 相机到目标点距离 |
| `target` | `glm::vec3` | `(0,0,0)` | 注视目标点（世界坐标） |
| `rotateSensitivity` | `float` | `0.15` | 旋转灵敏度（像素→角度） |
| `panSensitivity` | `float` | `0.001` | 平移灵敏度（相对距离比例） |
| `zoomSensitivity` | `float` | `0.3` | 缩放灵敏度（滚轮→距离） |
| `minDist` | `float` | `0.1` | 最小缩放距离 |
| `maxDist` | `float` | `50.0` | 最大缩放距离 |

#### 公开方法

```cpp
// 计算相机在世界空间的位置（由球坐标转换）
glm::vec3 eye() const;

// 生成观察矩阵（View Matrix）
glm::mat4 viewMatrix() const;

// 旋转：dx/dy 为鼠标移动像素量
// pitch 限制在 [-89°, 89°] 防止万向锁
void rotate(float dx, float dy);

// 平移：移动 target，速度与 distance 成正比
void pan(float dx, float dy);

// 缩放：delta 为滚轮增量（正值放大），限制在 [minDist, maxDist]
void zoom(float delta);
```

#### 使用示例

```cpp
Camera cam;
cam.target = model.computeCenter();
cam.distance = model.computeSize() * 2.0f;
cam.yaw = 45.0f;
cam.pitch = 30.0f;

glm::mat4 view = cam.viewMatrix();
glm::vec3 eyePos = cam.eye();
```

---

### 4.2 GLWidget — OpenGL 渲染窗口

**头文件**：`GLWidget.h`

继承自 `QOpenGLWidget`，是渲染和交互的核心组件。

#### 构造函数

```cpp
explicit GLWidget(QWidget* parent = nullptr);
```

#### 网格与颜色

| 方法 | 说明 |
|------|------|
| `void setMesh(const Mesh& mesh)` | 设置要渲染的三角网格，触发 GPU 上传 |
| `void setVertexColors(const std::vector<float>& colors)` | 设置 per-vertex RGB 颜色（用于云图） |
| `void setObjectColor(const glm::vec3& c)` | 设置统一物体颜色 |
| `void setUseVertexColor(bool use)` | 切换云图模式 (`true`) / 纯色模式 (`false`) |
| `void setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands)` | 上传 per-vertex 标量值，由 GPU 着色器做量化 + 颜色映射 |

#### 相机与视图

| 方法 | 说明 |
|------|------|
| `void fitToModel(const glm::vec3& center, float size)` | 自适应缩放，将模型居中并适配视口 |

#### 色标控制

| 方法 | 说明 |
|------|------|
| `void setColorBarVisible(bool visible)` | 显示/隐藏色标 |
| `void setColorBarRange(float min, float max)` | 设置色标值域范围 |
| `void setColorBarTitle(const QString& title)` | 设置色标标题 |

#### 拾取映射表

在设置 Mesh 后，需传入映射表以启用拾取功能：

| 方法 | 说明 |
|------|------|
| `void setTriangleToElementMap(const std::vector<int>& map)` | 三角形→单元 ID 映射 |
| `void setVertexToNodeMap(const std::vector<int>& map)` | 顶点→节点 ID 映射 |
| `void setTriangleToPartMap(const std::vector<int>& map)` | 三角形→部件索引映射 |
| `void setEdgeToPartMap(const std::vector<int>& map)` | 边线→部件索引映射 |

#### 拾取模式

```cpp
void setPickMode(PickMode mode);  // Node / Element / Part
```

#### 部件可见性（Slot）

```cpp
public slots:
    void setPartVisibility(int partIndex, bool visible);
    void highlightParts(const std::vector<int>& partIndices);
```

#### 查询方法

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `selection()` | `const FESelection&` | 当前选中状态 |
| `partColors()` | `const std::vector<glm::vec3>&` | 各部件渲染颜色 |
| `glRenderer()` | `QString` | GPU 渲染器名称 |
| `glVersion()` | `QString` | OpenGL 版本 |
| `glslVersion()` | `QString` | GLSL 版本 |
| `gpuVendor()` | `QString` | GPU 厂商 |
| `vertexCount()` | `int` | 当前渲染顶点数 |
| `triangleCount()` | `int` | 当前渲染三角形数 |
| `currentFps()` | `float` | 当前帧率 |
| `frameTimeMs()` | `float` | 单帧渲染耗时 (ms) |

#### 信号 (Signals)

```cpp
signals:
    // OpenGL 上下文初始化完成
    void glInitialized();

    // 选中状态变化
    // mode: 拾取模式, count: 选中数量, ids: 选中的 ID 列表
    void selectionChanged(PickMode mode, int count, const std::vector<int>& ids);

    // 部件拾取（用于同步 UI 模型树的选中状态）
    void partsPicked(const std::vector<int>& partIndices);
```

#### 鼠标交互行为

| 操作 | 行为 |
|------|------|
| 左键拖拽 | 旋转（轨道相机） |
| 中键拖拽 / 右键拖拽 | 平移 |
| 滚轮 | 缩放 |
| 左键单击 | 点选（根据 PickMode 选中节点/单元/部件） |
| Ctrl + 左键单击 | 追加/取消选中（多选） |
| 左键拖拽框选 | 框选 |

---

## 5. 交互层 API

### 5.1 PickMode — 拾取模式

**头文件**：`FEPickResult.h`

```cpp
enum class PickMode {
    Node,      // 选中最近的节点
    Element,   // 选中点击处的单元
    Part       // 选中整个部件
};
```

### 5.2 FEPickResult — 拾取结果

**头文件**：`FEPickResult.h`

描述单次鼠标拾取的结果。

```cpp
struct FEPickResult {
    bool      hit;             // 是否命中有效实体
    int       nodeId;          // 命中的节点 ID（Node 模式，默认 -1）
    int       elementId;       // 命中的单元 ID（Element/Part 模式，默认 -1）
    int       faceIndex;       // 命中的面索引（默认 -1）
    glm::vec3 worldPos;        // 命中点世界坐标
    float     depth;           // 命中点深度值
    int       triangleIndex;   // 命中的渲染三角形索引
};
```

### 5.3 FESelection — 选中状态

**头文件**：`FEPickResult.h`

维护当前被选中的节点和单元集合。

```cpp
struct FESelection {
    std::unordered_set<int> selectedNodes;      // 选中的节点 ID
    std::unordered_set<int> selectedElements;   // 选中的单元 ID

    void clear();                               // 清空所有选中
    bool isNodeSelected(int nodeId) const;      // 节点是否被选中
    bool isElementSelected(int elemId) const;   // 单元是否被选中
    void toggleNode(int nodeId);                // 切换节点选中状态
    void toggleElement(int elemId);             // 切换单元选中状态
    int  selectedNodeCount() const;             // 选中节点数
    int  selectedElementCount() const;          // 选中单元数
    bool hasSelection() const;                  // 是否有任何选中
};
```

---

## 6. 完整使用示例

### 6.1 加载模型并渲染

```cpp
#include <QApplication>
#include <QSurfaceFormat>
#include "FEModel.h"
#include "FEMeshConverter.h"
#include "GLWidget.h"

int main(int argc, char* argv[]) {
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(32);
    fmt.setSamples(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // ── 1. 构建模型（实际项目中从文件解析） ──
    FEModel model;
    model.name = "SimpleBox";

    // 8 个节点构成一个六面体
    model.addNode(1, {0,0,0}); model.addNode(2, {1,0,0});
    model.addNode(3, {1,1,0}); model.addNode(4, {0,1,0});
    model.addNode(5, {0,0,1}); model.addNode(6, {1,0,1});
    model.addNode(7, {1,1,1}); model.addNode(8, {0,1,1});

    model.addElement(1, ElementType::HEX8, {1,2,3,4,5,6,7,8});

    // ── 2. 转换为渲染数据 ──
    FERenderData rd = FEMeshConverter::toRenderData(model);

    // ── 3. 创建 GLWidget 并传入数据 ──
    GLWidget viewer;
    viewer.setMesh(rd.mesh);
    viewer.setTriangleToElementMap(rd.triangleToElement);
    viewer.setVertexToNodeMap(rd.vertexToNode);
    viewer.setTriangleToPartMap(rd.triangleToPart);
    viewer.setEdgeToPartMap(rd.edgeToPart);
    viewer.fitToModel(model.computeCenter(), model.computeSize());

    viewer.resize(1024, 768);
    viewer.setWindowTitle("FERender Example");
    viewer.show();

    return app.exec();
}
```

### 6.2 显示标量云图

```cpp
// 假设已有 model 和 viewer

// 创建 Von Mises 应力场
FEScalarField stress;
stress.name = "Von Mises";
stress.unit = "MPa";
stress.location = FieldLocation::Node;
for (auto& [id, node] : model.nodes) {
    stress.values[id] = /* 从求解器获取 */ ;
}

// 获取值域
float sMin, sMax;
stress.computeRange(sMin, sMax);

// 方式 A：CPU 颜色映射（通过 FEMeshConverter）
ColorMap cmap;
cmap.type = ColorMapType::Jet;
cmap.discreteLevels = 12;
FERenderData coloredRd = FEMeshConverter::toColoredRenderData(model, stress, cmap, sMin, sMax);
viewer.setMesh(coloredRd.mesh);
viewer.setUseVertexColor(true);

// 方式 B：GPU 着色器量化（推荐，性能更好）
FERenderData rd = FEMeshConverter::toRenderData(model);
viewer.setMesh(rd.mesh);
viewer.setVertexToNodeMap(rd.vertexToNode);

// 构建 per-vertex 标量数组（按渲染顶点顺序）
std::vector<float> scalars(rd.vertexCount());
for (int i = 0; i < rd.vertexCount(); ++i) {
    int nodeId = rd.vertexToNode[i];
    auto it = stress.values.find(nodeId);
    scalars[i] = (it != stress.values.end()) ? it->second : 0.0f;
}
viewer.setVertexScalars(scalars, sMin, sMax, 12);

// 显示色标
viewer.setColorBarVisible(true);
viewer.setColorBarRange(sMin, sMax);
viewer.setColorBarTitle("Von Mises Stress [MPa]");
```

### 6.3 部件可见性控制

```cpp
// 假设模型有 3 个部件
FEPart p1, p2, p3;
p1.name = "Housing"; p1.elementIds = {1,2,3};
p2.name = "Shaft";   p2.elementIds = {4,5};
p3.name = "Bearing"; p3.elementIds = {6,7,8,9};
model.parts = {p1, p2, p3};

FERenderData rd = FEMeshConverter::toRenderData(model);
viewer.setMesh(rd.mesh);
viewer.setTriangleToPartMap(rd.triangleToPart);
viewer.setEdgeToPartMap(rd.edgeToPart);

// 隐藏 Housing 部件
viewer.setPartVisibility(0, false);

// 高亮 Shaft 和 Bearing
viewer.highlightParts({1, 2});

// 读取部件颜色（用于 UI 显示色块）
const auto& colors = viewer.partColors();
// colors[0] → Housing 的 RGB
// colors[1] → Shaft 的 RGB
// colors[2] → Bearing 的 RGB
```

### 6.4 拾取交互

```cpp
// 设置拾取模式
viewer.setPickMode(PickMode::Element);

// 连接选中信号
QObject::connect(&viewer, &GLWidget::selectionChanged,
    [](PickMode mode, int count, const std::vector<int>& ids) {
        if (mode == PickMode::Element) {
            qDebug() << "Selected" << count << "elements:";
            for (int id : ids) qDebug() << "  Element" << id;
        }
    });

// 连接部件拾取信号（Part 模式下）
QObject::connect(&viewer, &GLWidget::partsPicked,
    [](const std::vector<int>& partIndices) {
        for (int idx : partIndices)
            qDebug() << "Part" << idx << "picked";
    });

// 程序化查询选中状态
const FESelection& sel = viewer.selection();
if (sel.hasSelection()) {
    qDebug() << "Nodes:" << sel.selectedNodeCount()
             << "Elements:" << sel.selectedElementCount();

    // 检查特定节点是否被选中
    if (sel.isNodeSelected(42)) {
        qDebug() << "Node 42 is selected";
    }
}
```

### 6.5 变形显示

```cpp
// 假设有位移矢量场
FEVectorField disp;
disp.name = "Displacement";
disp.unit = "mm";
disp.values[1] = {0.0f,  0.0f,  0.0f};
disp.values[2] = {0.5f,  0.0f, -0.1f};
disp.values[3] = {0.8f,  0.0f, -0.3f};
// ...

// 生成变形网格（放大 20 倍以便观察微小变形）
Mesh deformed = FEMeshConverter::toDeformedMesh(model, disp, 20.0f);

// 渲染变形后的网格
viewer.setMesh(deformed);
viewer.fitToModel(model.computeCenter(), model.computeSize());
```

---

## 附录 A：头文件清单

| 头文件 | 主要类型 | 说明 |
|--------|----------|------|
| `FENode.h` | `FENode` | 节点数据结构 |
| `FEElement.h` | `FEElement`, `ElementType` | 单元数据结构与类型枚举 |
| `FEGroup.h` | `FEPart`, `FENodeSet`, `FEElementSet` | 分组结构 |
| `FEModel.h` | `FEModel` | 模型顶层容器 |
| `FEField.h` | `FEScalarField`, `FEVectorField`, `ColorMap`, `ColorMapType` | 结果场与色谱 |
| `FEResultData.h` | `FEResultData`, `FESubcase`, `FEResultType`, `FEResultComponent` | 多工况结果层级 |
| `Geometry.h` | `Mesh`, `Geometry::*` | 网格结构与几何体生成 |
| `FERenderData.h` | `FERenderData` | 渲染数据包（Mesh + 映射表） |
| `FEMeshConverter.h` | `FEMeshConverter` | 网格转换器 |
| `Camera.h` | `Camera` | 轨道相机 |
| `GLWidget.h` | `GLWidget` | OpenGL 渲染窗口 |
| `FEPickResult.h` | `PickMode`, `FEPickResult`, `FESelection` | 拾取与选中 |
| `ferender_export.h` | `FERENDER_EXPORT` 宏 | DLL 导出宏（自动生成） |

---

## 附录 B：单元类型速查表

| 类型 | 维度 | 节点数 | 角节点数 | 说明 |
|------|------|--------|----------|------|
| `BAR2` | 1D | 2 | 2 | 线性杆单元 |
| `BAR3` | 1D | 3 | 2 | 二次杆单元（含中点） |
| `TRI3` | 2D | 3 | 3 | 线性三角形 |
| `TRI6` | 2D | 6 | 3 | 二次三角形（3 角点 + 3 边中点） |
| `QUAD4` | 2D | 4 | 4 | 线性四边形 |
| `QUAD8` | 2D | 8 | 4 | 二次四边形（4 角点 + 4 边中点） |
| `TET4` | 3D | 4 | 4 | 线性四面体 |
| `TET10` | 3D | 10 | 4 | 二次四面体（4 角点 + 6 边中点） |
| `HEX8` | 3D | 8 | 8 | 线性六面体 |
| `HEX20` | 3D | 20 | 8 | 二次六面体（8 角点 + 12 边中点） |
| `WEDGE6` | 3D | 6 | 6 | 三棱柱（楔形体） |
| `PYRAMID5` | 3D | 5 | 5 | 四棱锥 |

**三角化产生的三角形数**（用于估算渲染开销）：

| 单元类型 | 渲染三角形数 | 说明 |
|----------|-------------|------|
| TRI3 | 1 | 本身即三角形 |
| QUAD4 | 2 | 拆分为 2 个三角形 |
| TET4 | 4 | 4 个三角面 |
| HEX8 | 12 | 6 面 × 2 三角形 |
| WEDGE6 | 8 | 2 三角面 + 3 四边形面(×2) |
| PYRAMID5 | 6 | 1 底面(×2) + 4 三角面 |

> **注意**：3D 实体单元仅渲染外表面。共享面（两个单元共有的面）会被自动剔除，实际三角形数通常远小于理论最大值。
