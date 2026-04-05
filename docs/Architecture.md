# FEModelViewer 项目架构详解

## 目录

1. [项目概览](#1-项目概览)
2. [整体架构](#2-整体架构)
3. [数据层（FEM 数据结构）](#3-数据层fem-数据结构)
4. [解析层（文件解析器）](#4-解析层文件解析器)
5. [转换层（Mesh 转换器）](#5-转换层mesh-转换器)
6. [渲染层（OpenGL 渲染）](#6-渲染层opengl-渲染)
7. [交互层（拾取系统）](#7-交互层拾取系统)
8. [GUI 层（Qt 面板）](#8-gui-层qt-面板)
9. [着色器系统](#9-着色器系统)
10. [主题系统](#10-主题系统)
11. [完整数据流](#11-完整数据流)
12. [构建系统](#12-构建系统)

---

## 1. 项目概览

**FEModelViewer** 是一个基于 **Qt5 + OpenGL 4.1** 的有限元（FEM）模型查看器，用于加载、显示和交互式查看有限元分析模型及其结果。

### 核心能力

| 功能 | 说明 |
|------|------|
| 多格式解析 | Abaqus INP、Nastran BDF、Nastran OP2（二进制）、UNV |
| 3D 渲染 | 实体/线框/混合渲染模式，flat shading |
| 云图显示 | 标量场 → Jet 色谱映射，分段色阶 |
| 交互拾取 | GPU Color Picking，支持点选/框选节点/单元/部件 |
| 部件管理 | 按部件显隐、多色区分、模型树浏览 |
| 多主题 | 6 种内置主题（深色/浅色/深海蓝/森林/暮光/北欧极光） |

### 模块划分

项目分为两大模块：

```
┌─────────────────────────────────────────────────┐
│            FEModelViewer（GUI 应用）              │
│  MainWindow + 各功能面板（*Panel）               │
│  链接 FERender 共享库                            │
├─────────────────────────────────────────────────┤
│            FERender（共享库，可独立安装）          │
│  数据层 + 解析层 + 转换层 + 渲染层               │
│  标记 FERENDER_EXPORT 的类均可外部调用           │
└─────────────────────────────────────────────────┘
```

---

## 2. 整体架构

### 分层架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      GUI 层                                  │
│  MainWindow / FEModelPanel / PartsPanel / ResultPanel        │
│  ControlPanel / MonitorPanel / PickPanel                     │
├─────────────────────────────────────────────────────────────┤
│                      渲染层                                  │
│  GLWidget (QOpenGLWidget + QOpenGLFunctions)                 │
│  Camera (轨道相机)                                           │
│  4 套 Shader (scene / pick / background / axes)              │
├─────────────────────────────────────────────────────────────┤
│                      转换层                                  │
│  FEMeshConverter (FEModel → FERenderData)                    │
│  三角化 + 面提取 + 反向映射表生成                              │
├─────────────────────────────────────────────────────────────┤
│                      解析层                                  │
│  FEParser (静态工具类)                                       │
│  INP / BDF / OP2 / UNV 文件解析                              │
├─────────────────────────────────────────────────────────────┤
│                      数据层                                  │
│  FEModel / FENode / FEElement / FEGroup                      │
│  FEField / FEResultData / FEPickResult                       │
│  Geometry (Mesh 结构 + 基础几何体)                            │
└─────────────────────────────────────────────────────────────┘
```

### 核心设计原则

1. **数据层与渲染层分离**：FEModel 不依赖 OpenGL，纯数据结构
2. **转换层无状态**：FEMeshConverter 是静态工具类，可单独调用
3. **反向映射表驱动拾取**：渲染三角形 → FEM 单元的映射使拾取成为 O(1) 操作
4. **信号槽解耦**：GUI 面板之间通过 Qt 信号槽通信，不直接引用

---

## 3. 数据层（FEM 数据结构）

### 3.1 FENode — 节点

**文件**: `FENode.h`

有限元模型的最基本单元。每个节点有全局唯一 ID 和三维坐标。

```cpp
struct FENode {
    int id = 0;                              // 节点全局编号
    glm::vec3 coords{0.0f, 0.0f, 0.0f};     // 坐标 (x, y, z)
};
```

**设计要点**：
- ID 不要求连续（实际 FEM 模型中 ID 常有跳跃），所以 FEModel 用 `unordered_map<int, FENode>` 而非 `vector`
- 坐标用单精度 `glm::vec3`，满足可视化需求

### 3.2 FEElement — 单元

**文件**: `FEElement.h`

单元定义节点之间的连接关系（拓扑），是有限元网格的核心。

```cpp
enum class ElementType {
    BAR2, BAR3,                   // 1D 线单元
    TRI3, TRI6, QUAD4, QUAD8,    // 2D 壳/板单元
    TET4, TET10, HEX8, HEX20,   // 3D 实体单元
    WEDGE6, PYRAMID5             // 3D 特殊单元
};

struct FEElement {
    int id = 0;
    ElementType type = ElementType::TRI3;
    std::vector<int> nodeIds;      // 节点 ID 列表（非索引）
};
```

**单元类型总览**：

| 维度 | 类型 | 节点数 | 说明 |
|------|------|--------|------|
| 1D | BAR2/BAR3 | 2/3 | 杆单元 |
| 2D | TRI3/TRI6 | 3/6 | 三角形（线性/二次） |
| 2D | QUAD4/QUAD8 | 4/8 | 四边形（线性/二次） |
| 3D | TET4/TET10 | 4/10 | 四面体 |
| 3D | HEX8/HEX20 | 8/20 | 六面体 |
| 3D | WEDGE6 | 6 | 三棱柱 |
| 3D | PYRAMID5 | 5 | 四棱锥 |

辅助函数：
- `elementDimension(type)` → 返回 1/2/3（维度）
- `elementCornerNodeCount(type)` → 返回角节点数（不含中间节点）

### 3.3 FEGroup — 分组

**文件**: `FEGroup.h`

节点和单元的逻辑分组，三种类型：

```cpp
struct FENodeSet {                    // 节点集（如"固定端节点"）
    std::string name;
    std::vector<int> nodeIds;
};

struct FEElementSet {                 // 单元集（如"钢材部件"）
    std::string name;
    std::vector<int> elementIds;
};

struct FEPart {                       // 部件（模型的一个零件）
    std::string name;
    std::vector<int> nodeIds;
    std::vector<int> elementIds;
    bool visible = true;              // UI 控制显隐
};
```

**为什么用 vector 而非 set**：遍历性能好、内存连续、加载后不频繁增删。

### 3.4 FEModel — 模型顶层容器

**文件**: `FEModel.h` / `FEModel.cpp`

整个有限元数据的统一入口：

```
FEModel
├── nodes        : unordered_map<int, FENode>     节点表
├── elements     : unordered_map<int, FEElement>   单元表
├── parts        : vector<FEPart>                  部件列表
├── nodeSets     : vector<FENodeSet>               节点集
├── elementSets  : vector<FEElementSet>            单元集
├── scalarFields : vector<FEScalarField>           标量场
└── vectorFields : vector<FEVectorField>           矢量场
```

关键方法：
- `addNode(id, coords)` / `addElement(id, type, nodeIds)` — 添加数据
- `nodeCoords(id)` → 返回 `const glm::vec3*`（nullptr 表示未找到）
- `computeBoundingBox(bbMin, bbMax)` — 计算 AABB 包围盒
- `computeCenter()` / `computeSize()` — 相机自动定位用
- `clear()` / `isEmpty()` — 生命周期管理

### 3.5 FEField — 结果场与色谱

**文件**: `FEField.h` / `FEField.cpp`

后处理的核心：将 FEM 求解结果映射到颜色。

```
FEM 求解结果 → FEScalarField (每节点/单元一个值)
                    ↓
            ColorMap::map(value, min, max)
                    ↓
            RGB 颜色 → 传入顶点颜色 → GPU 渲染云图
```

**标量场** `FEScalarField`：
```cpp
struct FEScalarField {
    std::string name;           // 如 "Von Mises Stress"
    std::string unit;           // 如 "MPa"
    FieldLocation location;     // Node 或 Element
    unordered_map<int, float> values;   // ID → 值
};
```

**矢量场** `FEVectorField`：
```cpp
struct FEVectorField {
    std::string name;
    std::string unit;
    FieldLocation location;
    unordered_map<int, glm::vec3> values;  // ID → (x,y,z)
};
```

**色谱映射** `ColorMap`：

5 种色谱类型：
| 类型 | 效果 | 适用场景 |
|------|------|----------|
| Rainbow | 蓝→青→绿→黄→红 | 通用 |
| Jet | 类似 MATLAB 默认 | 通用 |
| CoolWarm | 蓝→白→红 | 正负值对比 |
| Grayscale | 黑→白 | 打印友好 |
| Viridis | 深紫→蓝绿→亮黄 | 色盲友好 |

支持分段色阶 (`discreteLevels`)：当 `> 0` 时值被量化到离散色带。

### 3.6 FEResultData — 结果数据层级

**文件**: `FEResultData.h`

多级层次结构，对应 FEM 后处理的组织方式：

```
FEResultData              顶层容器
  └─ FESubcase             工况 (id + name)
       └─ FEResultType     结果类型 (如"位移"/"应力")
            └─ FEResultComponent  单个分量 (如 "X"/"Von Mises")
                 └─ FEScalarField     标量场数据
```

### 3.7 FEPickResult — 拾取结果

**文件**: `FEPickResult.h`

三个拾取模式：
```cpp
enum class PickMode { Node, Element, Part };
```

**FEPickResult** — 单次拾取的结果：
```cpp
struct FEPickResult {
    bool hit = false;
    int nodeId = -1;        // 节点拾取
    int elementId = -1;     // 单元拾取
    int faceIndex = -1;     // 面索引
    glm::vec3 worldPos;     // 交点世界坐标
    float depth;            // 深度值
    int triangleIndex = -1; // 渲染三角形索引
};
```

**FESelection** — 持久选中状态：
```cpp
struct FESelection {
    unordered_set<int> selectedNodes;
    unordered_set<int> selectedElements;
    // toggle / isSelected / clear 等方法
};
```

### 3.8 Geometry / Mesh — 渲染网格

**文件**: `Geometry.h` / `Geometry.cpp`

`Mesh` 是最底层的渲染数据结构：

```cpp
struct Mesh {
    vector<float> vertices;          // [px,py,pz, nx,ny,nz, ...]（每顶点 6 float）
    vector<unsigned int> indices;    // 三角形索引（每 3 个一组）
    vector<float> edgeVertices;      // 边线顶点
    vector<unsigned int> edgeIndices;// 边线索引
    // + 单元边线用于选中高亮
};
```

**Geometry 命名空间** 提供 7 种基础几何体生成器，用于演示和测试：
- `cube()` / `tetrahedron()` / `triangularPrism()`
- `cylinder(segments)` / `cone(segments)`
- `sphere(rings, sectors)` / `torus(ringSegs, tubeSegs)`

### 3.9 FERenderData — 渲染数据包

**文件**: `FERenderData.h`

FEMeshConverter 的输出，捆绑了渲染数据 + 反向映射表：

```
FERenderData
├── mesh                    三角网格
├── triangleToElement[]     三角形 → FEM 单元 ID
├── triangleToFace[]        三角形 → 单元内面序号
├── vertexToNode[]          渲染顶点 → FEM 节点 ID
├── triangleToPart[]        三角形 → 部件索引
└── edgeToPart[]            边线 → 部件索引
```

**为什么需要反向映射**：

一个 FEM 单元会生成多个渲染三角形（例如 HEX8 → 12 个三角形）。用户点击屏幕上的三角形后，需要反查"这个三角形属于哪个 FEM 单元"。映射表使用 `vector<int>`，索引对齐，O(1) 查找。

---

## 4. 解析层（文件解析器）

### FEParser — 无状态静态工具类

**文件**: `FEParser.h` + 5 个实现文件

```cpp
class FEParser {
public:
    static bool parseAbaqusInp(path, model, progress);     // INP 格式
    static bool parseNastranBdf(path, model, progress);    // BDF/FEM 格式
    static bool parseNastranOp2(path, model, progress);    // OP2 几何
    static bool parseNastranOp2Results(path, results);     // OP2 结果
    static bool parseUnvResults(path, results);            // UNV 结果
};
```

### 支持的格式

| 格式 | 文件后缀 | 实现文件 | 解析内容 |
|------|----------|----------|----------|
| Abaqus INP | .inp | `FEParser_inp.cpp` | 节点/单元/部件/集合（支持 INCLUDE 展开） |
| Nastran BDF | .bdf/.fem | `FEParser_bdf.cpp` | 节点/单元（固定/自由格式，CORD2R 坐标系变换） |
| Nastran OP2 几何 | .op2 | `FEParser_op2.cpp` | 二进制格式的节点/单元/部件 |
| Nastran OP2 结果 | .op2 | `FEParser_op2results.cpp` | 位移/应力结果（多工况） |
| UNV 结果 | .unv | `FEParser_unv.cpp` | Dataset 2414/55 结果数据 |

### 解析流程

```
用户选择文件 → FEModelPanel::loadModelFromPath(path)
  → 根据后缀名选择解析器
  → FEParser::parseXxx(path, model, progress)
  → 填充 FEModel 对象
  → FEMeshConverter::toRenderData(model) 转换为渲染数据
  → 发射 meshGenerated 信号 → GLWidget 接收并渲染
```

---

## 5. 转换层（Mesh 转换器）

### FEMeshConverter — FEModel → FERenderData

**文件**: `FEMeshConverter.h` / `FEMeshConverter.cpp`

这是数据层和渲染层之间的桥梁。

### 核心算法

#### 1. 单元分类与三角化

```
FEModel (节点+单元)
  ↓
单元按维度分类
  ├── 1D (BAR2/BAR3): 暂跳过
  ├── 2D (TRI3/QUAD4/...): tessellate2D() 直接三角化
  └── 3D (TET4/HEX8/...): 提取外表面 → tessellateFace()
```

#### 2. 2D 单元三角化 (tessellate2D)

- TRI3 → 1 个三角形
- QUAD4 → 2 个三角形：`(0,1,2) + (0,2,3)`
- TRI6 → 用角节点 3 个做线性近似
- QUAD8 → 用角节点 4 个做线性近似

每个三角形使用 **flat shading**（面法线 = 两条边的叉积），呈现棱角分明的效果。

#### 3. 3D 单元外表面提取

3D 实体单元的内部面不需要渲染。方法：
1. 遍历所有 3D 单元的所有面（`extractFaces`）
2. 用面的节点 ID 排序后作为 key
3. 被两个单元共享的面是内部面（去除）
4. 只被一个单元引用的面是外表面（保留并三角化）

`extractFaces` 返回值示例：
- TET4 → 4 个三角面
- HEX8 → 6 个四边形面（每面 4 节点）
- WEDGE6 → 2 个三角面 + 3 个四边形面

#### 4. 反向映射表

在三角化的同时，同步记录：
```
triangleToElement[i] = 第 i 个三角形来自哪个 FEM 单元
triangleToFace[i]    = 该三角形属于单元的第几个面
vertexToNode[i]      = 第 i 个渲染顶点对应哪个 FEM 节点
```

#### 5. 云图颜色映射 (toColoredRenderData)

```cpp
static FERenderData toColoredRenderData(
    const FEModel& model,
    const FEScalarField& field,    // 要显示的标量场
    const ColorMap& colorMap,       // 色谱
    float minVal, float maxVal      // 值域范围
);
```

将每个顶点的标量值通过 ColorMap 映射为 RGB 颜色。

### 其他接口

- `toDeformedMesh(model, displacement, scale)` — 变形显示
- `toWireframeMesh(model)` — 纯线框网格

---

## 6. 渲染层（OpenGL 渲染）

### 6.1 GLWidget — OpenGL 渲染组件

**文件**: `GLWidget.h` / `GLWidget.cpp`

继承自 `QOpenGLWidget + QOpenGLFunctions`，是整个渲染系统的核心。

#### OpenGL 资源管理

```
场景渲染:
  ├── shader_     (QOpenGLShaderProgram) — scene.vert + scene.frag
  ├── vao_/vbo_/ibo_ — 主网格 VAO/VBO/IBO
  ├── colorVbo_   — per-vertex 颜色（部件色/云图色）
  ├── scalarVbo_  — per-vertex 标量值（GPU 端色谱映射）
  └── triPartTbo_/triPartTex_ — 三角形→部件索引 texture buffer

边线渲染:
  ├── edgeVao_/edgeVbo_/edgeIbo_ — 网格边线
  └── selEdgeVao_/selEdgeVbo_ — 选中高亮边线

背景渲染:
  └── bgShader_ + bgVao_/bgVbo_ — 渐变背景

坐标轴:
  └── axesShader_ + axesVao_/axesVbo_ — 角落坐标轴指示器

拾取:
  ├── pickShader_ — pick.vert + pick.frag
  ├── pickFbo_    — 离屏 FBO（颜色ID编码）
  └── pickVao_    — 拾取专用 VAO
```

#### 渲染流程 (paintGL)

```
1. 处理延迟拾取请求（如有）
2. 清屏
3. 绘制渐变背景（bgShader_，全屏四边形）
4. 计算 MVP 矩阵（投影 × 视图 × 模型）
5. 绘制场景网格：
   a. 使用 scene shader
   b. 设置光照参数（方向光 + 补光 + Blinn-Phong 高光）
   c. 根据部件可见性过滤三角形
   d. 如果是云图模式，用 GPU 端标量值做色谱映射
   e. 如果不是云图，用 gl_PrimitiveID 查 texture buffer 确定部件颜色
6. 绘制边线（黑色线框叠加）
7. 绘制选中高亮边线（轮廓边 + 轮廓点）
8. 绘制坐标轴指示器（左下角）
9. 使用 QPainter 绘制 2D 覆盖层：
   - 坐标轴标签 (X/Y/Z)
   - 选中项 ID 标签
10. 更新 FPS 统计
```

#### 部件可见性系统

GLWidget 维护了部件级别的可见性控制：

```
setTriangleToPartMap(map)
  → 为每个部件预计算三角形列表和单元列表
  → partTriangles_[partIndex] = [tri0, tri1, ...]
  → partElementIds_[partIndex] = [elem0, elem1, ...]

setPartVisibility(partIndex, visible)
  → 更新 partVisibility_
  → 标记 partVisibilityDirty_ = true
  → 下一帧 paintGL 中重建 IBO（只包含可见部件的三角形）
```

### 6.2 Camera — 轨道相机

**文件**: `Camera.h` / `Camera.cpp`

球坐标系相机，围绕目标点旋转：

```
参数：
  yaw     — 水平旋转角（绕 Y 轴）
  pitch   — 垂直仰俯角
  distance — 相机到目标点的距离
  target  — 注视目标点

交互：
  rotate(dx, dy) → 改变 yaw/pitch（支持 360° 自由旋转，无万向锁）
  pan(dx, dy)    → 平移目标点（速度与距离成正比）
  zoom(delta)    → 按比例缩放距离
```

**关键实现细节**：
- `eye()` 方法：球坐标 → 笛卡尔坐标转换
- `viewMatrix()`：使用 `glm::lookAt`，根据 pitch 判断 up 方向是否翻转
- `rotate()`：当相机倒置时（`cos(pitch) < 0`），水平旋转方向取反
- `pan()`：从视线方向推导真实 right/up 向量，确保平移方向与屏幕对齐

---

## 7. 交互层（拾取系统）

### GPU Color Picking 方案

```
鼠标点击 (x, y)
      ↓
离屏渲染：每个三角形用唯一颜色 ID
  → 单元模式: 同一单元的所有三角形用同一颜色
  → 节点模式: 每个三角形的 3 个顶点各自编码
      ↓
glReadPixels 读取点击处像素
      ↓
颜色 → ID 解码
  → colorToId(r, g, b) = r + g*256 + b*65536
      ↓
反向映射查询
  → triToElem_[triangleIndex] → FEM 单元 ID
  → vertexToNode_[vertexIndex] → FEM 节点 ID
      ↓
更新 FESelection → 发射 selectionChanged 信号
      ↓
重建选中高亮边线 → 重绘
```

### 拾取模式

| 模式 | 编码方式 | 结果 |
|------|----------|------|
| Node | 渲染顶点 ID → 颜色 | 选中最近节点 |
| Element | 单元 ID → 颜色 | 选中整个单元 |
| Part | 同 Element，额外选中整个部件 | 选中部件所有单元 |

### 选中高亮

选中后的视觉反馈采用**轮廓边**方式：

1. **边缓存构建** (`buildPartEdgeCache`)：选中变化时执行（较重）
   - 收集选中单元/部件的所有边
   - 分类为：边界边、特征边（两侧法线夹角大）、轮廓边候选
2. **轮廓边刷新** (`updateSilhouetteFromCache`)：相机变化时执行（较轻）
   - 从缓存中筛选轮廓边（一侧朝向相机，一侧背离相机的边）
3. 最终用粗线（宽度 3px）+ 点阵渲染选中轮廓

### 框选

- 左键拖拽：框选添加（`QRubberBand` 显示框选矩形）
- 中键拖拽：框选取消
- 框选时批量读取 FBO 矩形区域内的颜色 ID

### 延迟拾取机制

为避免在 `paintGL` 外调用 `makeCurrent` 导致 GL 状态污染，拾取请求被延迟到下一帧：

```cpp
bool pickPointPending_ = false;
QPoint pendingPickPos_;
// mouseReleaseEvent 中设置 pending → update() 触发重绘
// paintGL 开头检查 pending → 执行拾取 → 清除 pending
```

---

## 8. GUI 层（Qt 面板）

### 8.1 MainWindow — 主窗口

**文件**: `MainWindow.h` / `MainWindow.cpp`

布局结构：

```
┌──────────────────────────────────────────────────┐
│  工具栏 (拾取模式：节点/单元/部件 + 主题切换)    │
├──────────┬──────────────────────┬────────────────┤
│ 左侧边栏 │                      │ 右侧边栏       │
│          │    GLWidget           │                │
│ PartsPanel│   (3D 视口)         │ PickPanel      │
│ (模型树) │                      │ (拾取控制)     │
│          │                      │                │
│FEModelPanel│                    │ ResultPanel    │
│ (模型信息)│                     │ (结果面板)     │
│          │                      │                │
│MonitorPanel│                    │                │
│ (性能监控)│                     │                │
├──────────┴──────────────────────┴────────────────┤
│  底部文件面板 (模型文件路径 + 结果文件路径 + 应用) │
├──────────────────────────────────────────────────┤
│  状态栏 (节点数 / 单元数 / 三角面数 + 进度条)    │
└──────────────────────────────────────────────────┘
```

### 8.2 FEModelPanel — 模型信息面板

**文件**: `FEModelPanel.h`

功能：
- 显示模型统计（节点数、单元数、三角面数、尺寸）
- 显示选中信息（模式、数量、ID 列表）
- ID 搜索功能（输入节点/单元 ID，跳转并选中）
- 加载文件逻辑（`loadModelFromFile` / `loadModelFromPath`）

信号：
- `meshGenerated(mesh, center, size, triToElem, vertexToNode)` — 网格转换完成
- `partsChanged(modelName, parts, triToPart, edgeToPart)` — 部件数据就绪
- `loadProgress(percent, text)` — 加载进度
- `resultsLoaded(results)` — 结果数据加载完成

### 8.3 PartsPanel — 部件模型树

**文件**: `PartsPanel.h`

以树形结构显示模型部件：
```
模型名称 (根节点)
  ├── [■] Part-1 (210 个单元)    ← 颜色色块 + 复选框
  ├── [■] Part-2 (156 个单元)
  └── [■] Part-3 (89 个单元)
```

- 复选框控制部件显隐 → `partVisibilityChanged` 信号 → GLWidget
- 多选高亮 → `partSelectionChanged` → GLWidget::highlightParts
- GLWidget 部件拾取 → `selectParts(indices)` 同步选中状态

### 8.4 ResultPanel — 结果面板

**文件**: `ResultPanel.h`

级联选择云图数据：
```
工况 (Subcase) → 结果类型 (Displacement/Stress) → 分量 (X/Y/Z/Magnitude/Von Mises)
```

点击"应用"发射 `applyResult(field, title)` 信号，触发云图渲染。

### 8.5 PickPanel — 拾取控制面板

**文件**: `PickPanel.h`

- 拾取模式切换（节点/单元/部件）
- 显示/隐藏控制
- ID 标签显示/隐藏

### 8.6 ControlPanel — 控制面板

**文件**: `ControlPanel.h`

- 形状选择（7 种基础几何体，用于演示）
- 显示模式（实体/线框/混合）
- 颜色选择（预设颜色列表）

### 8.7 MonitorPanel — 性能监控

**文件**: `MonitorPanel.h`

实时显示：
- FPS 帧率 / 每帧耗时
- 顶点数 / 三角面数
- GPU 型号 / 厂商 / OpenGL 版本 / GLSL 版本

通过 `bindToWidget(gl)` 绑定 GLWidget，200ms 定时刷新。

---

## 9. 着色器系统

项目使用 4 套 GLSL 着色器（OpenGL 4.1 Core Profile）：

### 9.1 Scene Shader — 场景渲染

**文件**: `shaders/scene.vert` + `shaders/scene.frag`

顶点属性（4 个 location）：
| Location | 属性 | 用途 |
|----------|------|------|
| 0 | aPos (vec3) | 顶点位置 |
| 1 | aNormal (vec3) | 法线 |
| 2 | aColor (vec3) | per-vertex 颜色 |
| 3 | aScalar (float) | per-vertex 标量值 |

片段着色器支持三种模式：
1. **线框模式** (`uWireframe = true`)：直接输出颜色
2. **云图模式** (`uContourMode = true`)：标量值 → 量化 → Jet 色谱
3. **部件颜色模式**：`gl_PrimitiveID` → texture buffer 查部件索引 → 调色板颜色

光照模型：**Blinn-Phong**
- 主方向光 + 补光（避免背光面全黑）
- 云图模式：高环境光(0.55) 保护色谱颜色，无高光
- 几何模式：柔和高光(0.10)，避免冲淡部件颜色
- 双面渲染：背面亮度 ×0.8

### 9.2 Pick Shader — 拾取渲染

**文件**: `shaders/pick.vert` + `shaders/pick.frag`

极简着色器：
- 顶点只有位置（location 0），无法线/颜色
- 片段直接输出 uniform `uPickColor`（编码为 ID 的颜色）
- 渲染到离屏 FBO，不显示在屏幕上

### 9.3 Background Shader — 渐变背景

**文件**: `shaders/background.vert` + `shaders/background.frag`

- 全屏四边形（2 个三角形）
- 顶点属性：位置(vec2) + 颜色(vec3)
- 顶部和底部各一种颜色，硬件插值产生渐变

### 9.4 Axes Shader — 坐标轴指示器

**文件**: `shaders/axes.vert` + `shaders/axes.frag`

- 在视口左下角绘制 XYZ 坐标轴
- 顶点属性：位置(vec3) + 颜色(vec3)（红/绿/蓝）
- 使用独立的 MVP 矩阵（只旋转，不平移/缩放）

---

## 10. 主题系统

**文件**: `Theme.h`

6 种内置主题，每个主题定义：

| 属性类别 | 字段数 | 说明 |
|----------|--------|------|
| UI 颜色 | 21 个 | Qt 样式表用的颜色字符串 |
| GL 背景 | 6 个 | 视口渐变顶/底色（float） |
| 色标文字 | 3 个 | 色标数值颜色（int RGB） |

主题列表：
| 索引 | 名称 | 风格 |
|------|------|------|
| 0 | 深色 | Catppuccin Mocha |
| 1 | 浅色 | Catppuccin Latte |
| 2 | 深海蓝 | 科技感蓝色调 |
| 3 | 森林 | 自然绿色调 |
| 4 | 暮光 | 暖色调 |
| 5 | 北欧极光 | Nord 冷灰蓝 |

所有面板统一实现 `applyTheme(const Theme&)` 方法。

---

## 11. 完整数据流

### 模型加载流程

```
用户点击"应用"（底部文件面板）
  │
  ▼
MainWindow::applyFiles()
  │
  ▼
FEModelPanel::loadModelFromPath(path)
  │
  ├── 根据后缀选择解析器
  │   ├── .inp → FEParser::parseAbaqusInp()
  │   ├── .bdf/.fem → FEParser::parseNastranBdf()
  │   └── .op2 → FEParser::parseNastranOp2()
  │
  ├── 填充 FEModel（节点/单元/部件）
  │
  ├── FEMeshConverter::toRenderData(model)
  │   ├── 2D 单元三角化
  │   ├── 3D 单元外表面提取 + 三角化
  │   └── 生成反向映射表
  │
  ├── 发射 meshGenerated 信号
  │   └── GLWidget::setMesh(mesh)
  │       └── 上传 VBO/IBO 到 GPU
  │
  └── 发射 partsChanged 信号
      ├── GLWidget::setTriangleToPartMap(map)
      └── PartsPanel::setParts(name, parts, colors)
```

### 云图显示流程

```
用户在 ResultPanel 选择工况/类型/分量，点击"应用"
  │
  ▼
ResultPanel::applyResult(field, title) 信号
  │
  ▼
MainWindow 接收信号
  │
  ├── 将 FEScalarField 转为 per-vertex 标量数组
  │   （通过 vertexToNode 映射表：渲染顶点 → 节点 → 标量值）
  │
  ├── GLWidget::setVertexScalars(scalars, min, max, bands)
  │   └── 上传标量到 scalarVbo_（GPU 端做量化+色谱映射）
  │
  ├── GLWidget::setColorBarVisible(true)
  ├── GLWidget::setColorBarRange(min, max)
  └── GLWidget::setColorBarTitle(title)
```

### 拾取交互流程

```
鼠标按下 (mousePressEvent)
  │
  ├── 左键：记录位置 → 拖拽 = 旋转，点击 = 拾取
  ├── 右键：平移
  └── 中键：框选取消
  │
鼠标释放 (mouseReleaseEvent)
  │
  ├── 如果没有拖拽（位移 < 5px）→ 标记 pickPointPending_
  │   └── update() 触发重绘
  │
  ▼
paintGL()
  │
  ├── 检查 pickPointPending_ → renderPickBuffer() + pickAtPoint()
  │   ├── 用 pickShader 渲染到 pickFbo_
  │   ├── glReadPixels 读像素颜色
  │   ├── colorToId() 解码
  │   ├── 更新 selection_
  │   ├── 重建选中高亮边线
  │   └── 发射 selectionChanged 信号
  │
  └── 正常渲染场景...
```

---

## 12. 构建系统

### CMakeLists.txt 结构

```
FEModelViewer (项目)
├── FERender (SHARED 库)
│   ├── 头文件：Camera.h, GLWidget.h, FEModel.h, FEParser.h, ...
│   ├── 源文件：Camera.cpp, GLWidget.cpp, FEModel.cpp, FEParser_*.cpp, ...
│   ├── Shader 资源：shaders.qrc
│   ├── 依赖：OpenGL, Qt5(Core/Gui/Widgets/OpenGL), GLM(header-only)
│   └── 导出宏：FERENDER_EXPORT（由 GenerateExportHeader 生成）
│
└── FEModelViewer (EXECUTABLE)
    ├── main.cpp, MainWindow.cpp, *Panel.cpp
    └── 链接 FERender
```

### 关键配置

- **C++ 标准**: C++17
- **OpenGL**: 4.1 Core Profile（macOS 支持的最高版本）
- **Qt**: 5.x（自动探测路径，支持 MinGW/MSVC/Homebrew）
- **GLM**: 0.9.9.8（通过 FetchContent 自动下载）
- **MSVC**: `/utf-8`（中文注释支持）
- **macOS**: `GL_SILENCE_DEPRECATION`（抑制 OpenGL 弃用警告）
- **安装**: `find_package(FERender)` 可被其他项目引用

### CLI 模式

程序支持不启动 GUI 的 CLI 模式：

```bash
./FEModelViewer --parse model.op2
```

仅解析文件并输出统计信息，用于批量测试。
