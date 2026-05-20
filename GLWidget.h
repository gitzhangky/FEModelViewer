/**
 * @file GLWidget.h
 * @brief OpenGL 渲染窗口组件声明
 *
 * GLWidget 继承自 QOpenGLWidget，负责：
 *   - 初始化 OpenGL 上下文、编译着色器
 *   - 上传网格数据到 GPU（VAO/VBO/IBO）
 *   - 每帧渲染场景（FE 平光 + 网格线框）
 *   - 处理鼠标/键盘事件（轨道交互 + 拾取调度）
 *   - 协调拾取、高亮、标签等内部渲染器
 *   - 统计 FPS 和查询硬件信息
 */

#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QRubberBand>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <vector>

#include <array>

#include "Camera.h"
#include "Geometry.h"
#include "FEPickResult.h"
#include "FEMeshConverter.h"   // FESurfaceCache：可见性变化时重建边界面
#include "ferender_export.h"

struct Theme;
class ColorBarOverlay;   // 前向声明：色标覆盖层控件
class LabelOverlay;
class PickRenderer;
class SelectionRenderer;

/** @brief 几何显示模式 */
enum class DisplayMode { Solid, Wireframe, SolidWireframe };

/** @brief 投影模式 */
enum class ProjectionMode { Perspective, Orthographic };

/** @brief 云图色谱方案 */
enum class Colormap { Jet, Grayscale, CoolWarm };

class FERENDER_EXPORT GLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
    friend class LabelOverlay;
    friend class PickRenderer;
    friend class SelectionRenderer;

public:
    explicit GLWidget(QWidget* parent = nullptr);

    /** @brief 设置要渲染的网格数据 */
    void setMesh(const Mesh& mesh);

    /**
     * @brief 设置表面缓存（面+单元邻接），启用"按可见集合重建边界面"
     *
     * 启用后，隐藏实体单元时会重新生成当前可见单元集合的边界面，
     * 包括暴露出来的切口内壁；拓扑映射表（elemToPart/elemToNodes 等）
     * 从缓存按全模型构建，保证隐藏/取消隐藏/拾取/部件着色均正确。
     * 应在 setMesh 及各映射表设置之后调用。
     */
    void setSurfaceCache(const FESurfaceCache& cache);

    /** @brief 直接设置 per-vertex 颜色到 colorVbo_（用于云图） */
    void setVertexColors(const std::vector<float>& colors);

    /** @brief 设置物体颜色 */
    void setObjectColor(const glm::vec3& c);

    /** @brief 设置边线颜色（实体+线框 / 线框模式下的边线） */
    void setEdgeColor(const glm::vec3& c);

    /** @brief 设置边线宽度（像素，按驱动能力钳制） */
    void setEdgeWidth(float w);
    /** @brief 设置边线不透明度 [0,1]（叠加在自适应淡化之上） */
    void setEdgeAlpha(float a);
    /** @brief 设置实体面不透明度 [0,1]（<1 开启混合） */
    void setSurfaceAlpha(float a);

    /** @brief 反转云图色谱（如 Jet 从蓝→红 变为 红→蓝） */
    void setColormapInverted(bool inverted);

    /** @brief 设置几何显示模式（实体 / 线框 / 实体+线框） */
    void setDisplayMode(DisplayMode mode);
    /** @brief 当前显示模式 */
    DisplayMode displayMode() const { return displayMode_; }

    /** @brief 设置投影模式（透视 / 正交） */
    void setProjectionMode(ProjectionMode mode);
    /** @brief 当前投影模式 */
    ProjectionMode projectionMode() const { return projectionMode_; }

    /** @brief 自定义背景上/下渐变颜色（覆盖主题背景） */
    void setBackgroundColors(const glm::vec3& top, const glm::vec3& bottom);
    /** @brief 背景颜色恢复为当前主题预设 */
    void resetBackgroundToTheme();

    /** @brief 设置云图色谱方案 */
    void setColormap(Colormap map);
    /** @brief 设置云图分段数（量化档数） */
    void setNumBands(int bands);

    /** @brief 自适应缩放 */
    void fitToModel(const glm::vec3& center, float size);

    /** @brief 应用主题（更新背景渐变和色标文字颜色） */
    void applyTheme(const Theme& theme);

    /** @brief 色标控制 */
    void setColorBarVisible(bool visible);
    void setColorBarRange(float min, float max);
    void setColorBarTitle(const QString& title);
    void setColorBarExtremes(int minId, float minVal, int maxId, float maxVal);
    void setColorBarIdLabel(const QString& label);

    /** @brief 设置三角形→单元映射表（用于拾取） */
    void setTriangleToElementMap(const std::vector<int>& map);

    /** @brief 设置三角形→面映射表（保留接口兼容性） */
    void setTriangleToFaceMap(const std::vector<int>& map) { (void)map; }

    /** @brief 设置渲染顶点→FEM节点ID映射表 */
    void setVertexToNodeMap(const std::vector<int>& map);

    /** @brief 设置拾取模式 */
    void setPickMode(PickMode mode);

    /** @brief 获取当前拾取模式 */
    PickMode pickMode() const { return pickMode_; }

    /** @brief 设置是否显示选中项的 ID 标签 */
    void setShowLabels(bool show);

    /** @brief 按 ID 列表选中节点或单元（供搜索框调用） */
    void selectByIds(PickMode mode, const std::vector<int>& ids);

    /** @brief 设置一组节点的可见性（隐藏节点会隐藏连接到这些节点的单元） */
    void setNodesVisibility(const std::vector<int>& nodeIds, bool visible);

    /** @brief 设置一组单元的可见性 */
    void setElementsVisibility(const std::vector<int>& elementIds, bool visible);

    /** @brief 设置未变形叠加网格（半透明线框显示原始形状） */
    void setOverlayMesh(const Mesh& mesh);

    /** @brief 控制叠加网格显隐 */
    void setOverlayVisible(bool visible);

    /** @brief 设置是否使用顶点颜色（云图模式） */
    void setUseVertexColor(bool use);

    /** @brief 设置切片交线（GL_LINES 显示） */
    void setSliceLines(const std::vector<float>& lineVertices);

    /** @brief 清除切片交线 */
    void clearSliceLines();

    /** @brief 设置等值面网格（半透明叠加显示） */
    void setIsoSurfaceMesh(const Mesh& mesh);

    /** @brief 清除等值面网格 */
    void clearIsoSurface();

    /** @brief 设置裁剪/切片平面预览（半透明平面） */
    void setClipPlanePreview(const glm::vec3& bbMin,
                             const glm::vec3& bbMax,
                             const glm::vec3& origin,
                             const glm::vec3& normal);

    /** @brief 清除裁剪/切片平面预览 */
    void clearClipPlanePreview();

    /** @brief 上传 per-vertex 标量值到 GPU，由片段着色器做量化 + 颜色映射 */
    void setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);

    /** @brief 设置三角形 → 部件索引映射表 */
    void setTriangleToPartMap(const std::vector<int>& map);

    /** @brief 设置边线 → 部件索引映射表（用于边线可见性控制） */
    void setEdgeToPartMap(const std::vector<int>& map);

    /** @brief 获取各部件的渲染颜色（供 PartsPanel 显示色块使用） */
    const std::vector<glm::vec3>& partColors() const { return partColors_; }

public slots:
    /** @brief 设置指定部件的可见性，触发重绘 */
    void setPartVisibility(int partIndex, bool visible);

    /** @brief 高亮显示指定部件（来自模型树多选） */
    void highlightParts(const std::vector<int>& partIndices);

    /** @brief 获取当前选中状态 */
    const FESelection& selection() const { return selection_; }

    // ── 硬件信息查询 ──
    QString glRenderer()  const { return glRenderer_; }
    QString glVersion()   const { return glVersion_; }
    QString glslVersion() const { return glslVersion_; }
    QString gpuVendor()   const { return gpuVendor_; }
    float   maxLineWidth() const { return maxLineWidth_; }

    // ── 网格统计 ──
    int vertexCount()     const;
    int triangleCount()   const;

    // ── 性能统计 ──
    float currentFps()    const { return fps_; }
    float frameTimeMs()   const { return frameTime_; }

signals:
    void glInitialized();
    void selectionChanged(PickMode mode, int count, const std::vector<int>& ids);
    /** @brief 部件拾取后发射选中的部件索引列表（用于同步模型树选中状态） */
    void partsPicked(const std::vector<int>& partIndices);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void uploadMesh();
    void drawAxesIndicator();
    void uploadColors();      // 将部件颜色上传到 colorVbo_
    void rebuildEdgeIbo();    // 根据部件可见性重建边线 IBO

    // 按驱动支持范围设置线宽。macOS Core Profile 只支持 1.0，超出会被钳制或在
    // 严格驱动上触发 GL_INVALID_VALUE，故统一走此包装。
    void setLineWidthClamped(float width);

    // 根据 projectionMode_ 构建投影矩阵（透视或正交），供绘制与拾取统一调用。
    glm::mat4 projectionMatrix(float aspect, float nearPlane, float farPlane) const;

    // 用 bgTopColor_/bgBotColor_ 重写背景渐变 VBO（需 GL 已初始化）。
    void uploadBackgroundVbo();

    // 从表面缓存按全模型构建稳定的拓扑映射（elemToPart_/elemToNodes_ 等）。
    void buildTopologyFromCache();
    // 据当前可见性用缓存重建边界面网格 + 渲染映射，并触发重上传（保留相机/选中）。
    void rebuildSurfaceFromCache();

    // ── paintGL 渲染子步骤 ──
    void rebuildPartVisibilityIbo();
    void renderBackground();
    void renderMainMesh();
    void renderMeshEdges();
    void renderOverlayMesh();
    void renderClipPreview();
    void renderSliceLines();
    void renderIsoSurface();
    void render2DOverlays(const glm::mat4& mvp);
    void updateFpsStats();
    void bindWidgetFramebuffer();
    void cleanupGLResources();
    void orbitAroundPivot(float dx, float dy);
    void rebuildElementNodeMap();
    void rebuildNodeVertexLookup();
    void rebuildRenderEdgeMaps();
    void markVisibilityDirty();
    bool isPartVisible(int partIndex) const;
    bool isElementVisible(int elemId) const;
    bool isElementRenderable(int elemId) const;
    bool isTriangleVisible(int triIndex) const;
    bool isNodeVisible(int nodeId) const;

    // ── 场景对象 ──
    Camera cam_;
    Mesh mesh_;
    bool needsUpload_ = true;
    bool glResourcesCleaned_ = false;

    // ── OpenGL 对象 ──
    QOpenGLShaderProgram* shader_ = nullptr;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer* ibo_ = nullptr;

    // ── 渐变背景 ──
    QOpenGLShaderProgram* bgShader_ = nullptr;
    QOpenGLVertexArrayObject bgVao_;
    QOpenGLBuffer bgVbo_{QOpenGLBuffer::VertexBuffer};

    // ── 边线 ──
    QOpenGLVertexArrayObject edgeVao_;
    QOpenGLBuffer edgeVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer* edgeIbo_ = nullptr;
    int edgeIndexCount_ = 0;

    // ── 拾取 / 高亮 / 标签（内部实现类，不暴露到安装头） ──
    std::shared_ptr<PickRenderer> pickRenderer_;
    std::shared_ptr<SelectionRenderer> selectionRenderer_;
    std::shared_ptr<LabelOverlay> labelOverlay_;
    std::vector<int> triToElem_;        // 三角形索引 → 单元 ID
    std::vector<int> vertexToNode_;     // 渲染顶点索引 → FEM 节点 ID
    PickMode pickMode_ = PickMode::Node;  // 当前拾取模式（与下拉框默认值同步）
    FESelection selection_;             // 当前选中状态
    bool showLabels_ = false;           // 是否显示选中项的 ID 标签

    // ── 部件可见性 ──
    std::vector<int> triToPart_;                        // 三角形 → 部件索引
    std::unordered_map<int, bool> partVisibility_;      // 部件索引 → 是否可见
    std::vector<unsigned int> allTriIndices_;            // 完整三角形索引（不过滤）
    int activeIndexCount_ = 0;                          // 当前实际绘制的索引数量
    bool partVisibilityDirty_ = false;                  // 是否需要重建 IBO

    // ── 部件颜色 ──
    std::vector<glm::vec3> partColors_;      // 每个部件的渲染颜色

    // ── 顶点颜色缓冲（per-vertex 部件色） ──
    QOpenGLBuffer colorVbo_{QOpenGLBuffer::VertexBuffer};
    bool needsColorUpload_ = false;          // 需要上传颜色数据到 GPU

    // ── 标量值缓冲（per-vertex，片段着色器量化） ──
    QOpenGLBuffer scalarVbo_{QOpenGLBuffer::VertexBuffer};
    float scalarMin_ = 0.0f;
    float scalarMax_ = 1.0f;
    int numBands_ = 10;

    // ── 部件索引 texture buffer（per-triangle，用 gl_PrimitiveID 查表） ──
    GLuint triPartTbo_ = 0;       // texture buffer object
    GLuint triPartTex_ = 0;       // texture 对象
    bool triPartDirty_ = false;   // 需要重新上传

    // ── 边线可见性 ──
    std::vector<int> edgeToPart_;            // 边线 → 部件索引
    std::vector<unsigned int> allEdgeIndices_;  // 完整边线索引（不过滤）
    int activeEdgeIndexCount_ = 0;           // 当前可见边线索引数量
    bool edgeVisibilityDirty_ = false;       // 需要重建边线 IBO

    // ── 预计算的每部件数据（在 setTriangleToPartMap 中构建） ──
    std::vector<std::vector<int>> partTriangles_;    // partIndex → 三角形索引列表
    std::vector<std::vector<int>> partElementIds_;   // partIndex → 去重单元 ID 列表
    std::unordered_map<int, int> elemToPart_;        // 单元 ID → 部件索引（快速反查）
    std::unordered_map<int, std::unordered_set<int>> elemToNodes_;  // 单元 ID → 节点 ID 集
    std::unordered_map<int, std::unordered_set<int>> nodeToElems_;  // 节点 ID → 相邻单元 ID 集
    std::unordered_map<int, int> nodeToFirstVertex_; // 节点 ID → 首个渲染顶点索引（标签/高亮快速定位）
    std::vector<std::vector<int>> renderEdgeToElems_; // 显示边线 → 相邻单元 ID 列表
    std::vector<std::pair<int, int>> renderEdgeNodeIds_; // 显示边线 → 节点 ID 对
    std::unordered_set<int> hiddenElements_;         // 被用户隐藏的单元 ID
    std::unordered_set<int> hiddenNodes_;            // 被用户隐藏的节点 ID

    // ── 色标（Colorbar） ──
    bool colorBarVisible_ = false;
    float colorBarMin_ = 0.0f;
    float colorBarMax_ = 1.0f;
    QString colorBarTitle_ = "Result";
    bool useVertexColor_ = false;       // 是否使用云图颜色（通过 colorVbo_）
    ColorBarOverlay* colorBarOverlay_ = nullptr;  // 独立覆盖层控件（raster 绘制，不受 GL 状态影响）
    QColor barTextColor_{255, 255, 255};            // 色标数值文字颜色（随主题变化）
    // 背景渐变颜色（initializeGL 使用，applyTheme 更新）
    float bgTopColor_[3] = {0.38f, 0.45f, 0.58f};
    float bgBotColor_[3] = {0.68f, 0.74f, 0.82f};
    // 当前主题的背景预设（用于"恢复主题"）
    float themeBgTop_[3] = {0.38f, 0.45f, 0.58f};
    float themeBgBot_[3] = {0.68f, 0.74f, 0.82f};

    // ── 表面缓存（按可见集合重建边界面，支持切口内壁）──
    FESurfaceCache surfaceCache_;
    bool           hasSurfaceCache_     = false;
    bool           surfaceRebuildDirty_ = false;

    // ── 显示 / 投影 / 色谱状态 ──
    DisplayMode    displayMode_    = DisplayMode::SolidWireframe;
    ProjectionMode projectionMode_ = ProjectionMode::Perspective;
    Colormap       colormap_       = Colormap::Jet;
    bool           colormapInverted_ = false;
    glm::vec3      edgeColor_{0.2f, 0.2f, 0.22f};
    float          edgeWidth_      = 1.0f;
    float          edgeAlpha_      = 1.0f;
    float          surfaceAlpha_   = 1.0f;

    // ── 交互状态 ──
    QPoint lastPos_;
    QPoint pressPos_;                   // 鼠标按下位置（区分点击和拖拽）
    bool isDragging_ = false;           // 是否正在拖拽旋转/平移
    bool isBoxSelecting_ = false;       // 是否正在框选（添加）
    bool isBoxDeselecting_ = false;     // 是否正在框选（取消）
    QRubberBand* rubberBand_ = nullptr; // 框选矩形
    QPoint boxOrigin_;                  // 框选起始点

    // ── 双击设置旋转中心 ──
    QPoint pendingPivotPos_;            // 待处理的双击屏幕位置
    bool pivotPending_ = false;         // 是否有待处理的旋转中心请求
    glm::vec3 pivotMarkerPos_{0.0f};    // 标记的世界坐标
    QElapsedTimer pivotMarkerClock_;    // 标记出现时间
    QTimer* pivotFadeTimer_ = nullptr;  // 淡出动画驱动定时器
    bool pivotMarkerActive_ = false;    // 标记是否正在显示
    GLuint depthResolveFbo_ = 0;        // MSAA 深度 resolve 用 FBO
    GLuint depthResolveTex_ = 0;        // resolve FBO 的深度纹理
    int depthResolveW_ = 0, depthResolveH_ = 0;
    glm::vec3 orbitPivot_{0.0f};        // 自定义轨道旋转轴心（世界坐标）
    bool hasCustomPivot_ = false;       // 是否启用自定义轴心
    glm::vec3 modelCenter_{0.0f};       // 模型几何中心（fitToModel 记录），用于 Space 重置旋转中心
    bool hasModelCenter_ = false;       // 是否已记录过模型中心
    QElapsedTimer pivotResetHintClock_;
    bool pivotResetHintActive_ = false;

    glm::vec3 color_{0.55f, 0.75f, 0.73f};

    // ── 叠加网格（未变形线框） ──
    Mesh overlayMesh_;
    QOpenGLVertexArrayObject overlayVao_;
    QOpenGLBuffer overlayVbo_{QOpenGLBuffer::VertexBuffer};
    int overlayVertCount_ = 0;
    bool overlayVisible_ = false;
    bool overlayNeedsUpload_ = false;

    // ── 切片交线 ──
    QOpenGLVertexArrayObject sliceVao_;
    QOpenGLBuffer sliceVbo_{QOpenGLBuffer::VertexBuffer};
    int sliceVertCount_ = 0;
    bool sliceNeedsUpload_ = false;

    // ── 等值面 ──
    Mesh isoMesh_;
    QOpenGLVertexArrayObject isoVao_;
    QOpenGLBuffer isoVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer* isoIbo_ = nullptr;
    int isoIndexCount_ = 0;
    bool isoNeedsUpload_ = false;

    // ── 裁剪/切片平面预览 ──
    Mesh clipPreviewMesh_;
    QOpenGLVertexArrayObject clipPreviewVao_;
    QOpenGLBuffer clipPreviewVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer* clipPreviewIbo_ = nullptr;
    QOpenGLVertexArrayObject clipPreviewEdgeVao_;
    QOpenGLBuffer clipPreviewEdgeVbo_{QOpenGLBuffer::VertexBuffer};
    int clipPreviewIndexCount_ = 0;
    int clipPreviewEdgeVertCount_ = 0;
    bool clipPreviewVisible_ = false;
    bool clipPreviewNeedsUpload_ = false;

    // ── 坐标轴指示器 ──
    QOpenGLShaderProgram* axesShader_ = nullptr;
    QOpenGLVertexArrayObject axesVao_;
    QOpenGLBuffer axesVbo_{QOpenGLBuffer::VertexBuffer};
    int axesLineCount_ = 0;
    int axesTriCount_ = 0;

    // ── 硬件信息 ──
    QString glRenderer_, glVersion_, glslVersion_, gpuVendor_;

    // 驱动支持的最大 aliased 线宽（initializeGL 中查询 GL_ALIASED_LINE_WIDTH_RANGE）。
    // macOS Core Profile 通常为 1.0；Windows 桌面驱动多为 10+。
    float maxLineWidth_ = 1.0f;

    // ── FPS 统计 ──
    QElapsedTimer fpsTimer_;
    int frameCount_ = 0;
    float fps_ = 0.0f;
    float frameTime_ = 0.0f;
};
