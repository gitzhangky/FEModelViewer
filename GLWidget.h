/**
 * @file GLWidget.h
 * @brief OpenGL 渲染窗口组件声明
 *
 * GLWidget 继承自 QOpenGLWidget，负责：
 *   - 初始化 OpenGL 上下文、编译着色器
 *   - 上传网格数据到 GPU（VAO/VBO/IBO）
 *   - 每帧渲染场景（FE 平光 + 网格线框）
 *   - 处理鼠标/键盘事件（轨道交互 + 拾取）
 *   - GPU Color Picking（点选 + 框选）
 *   - 统计 FPS 和查询硬件信息
 */

#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFramebufferObject>
#include <QElapsedTimer>
#include <QRubberBand>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_set>
#include <set>
#include <vector>

#include "Camera.h"
#include "Geometry.h"
#include "FEPickResult.h"

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit GLWidget(QWidget* parent = nullptr);

    /** @brief 设置要渲染的网格数据 */
    void setMesh(const Mesh& mesh);

    /** @brief 设置物体颜色 */
    void setObjectColor(const glm::vec3& c);

    /** @brief 自适应缩放 */
    void fitToModel(const glm::vec3& center, float size);

    /** @brief 设置三角形→单元映射表（用于拾取） */
    void setTriangleToElementMap(const std::vector<int>& map);

    /** @brief 设置三角形→面映射表（用于面拾取） */
    void setTriangleToFaceMap(const std::vector<int>& map) { triToFace_ = map; }

    /** @brief 设置拾取模式 */
    void setPickMode(PickMode mode) { pickMode_ = mode; }

    /** @brief 获取当前选中状态 */
    const FESelection& selection() const { return selection_; }

    // ── 硬件信息查询 ──
    QString glRenderer()  const { return glRenderer_; }
    QString glVersion()   const { return glVersion_; }
    QString glslVersion() const { return glslVersion_; }
    QString gpuVendor()   const { return gpuVendor_; }

    // ── 网格统计 ──
    int vertexCount()     const;
    int triangleCount()   const;

    // ── 性能统计 ──
    float currentFps()    const { return fps_; }
    float frameTimeMs()   const { return frameTime_; }

signals:
    void glInitialized();
    void selectionChanged(int selectedCount);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void uploadMesh();
    void drawAxesIndicator();

    // ── 拾取相关 ──
    void renderPickBuffer(const glm::mat4& mvp);
    void pickAtPoint(const QPoint& pos, bool ctrlHeld);
    void pickInRect(const QRect& rect);
    glm::vec3 idToColor(int id);
    int colorToId(unsigned char r, unsigned char g, unsigned char b);
    void rebuildSelectionEdges();

    // ── 场景对象 ──
    Camera cam_;
    Mesh mesh_;
    bool needsUpload_ = true;

    // ── OpenGL 对象 ──
    QOpenGLShaderProgram* shader_ = nullptr;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer* ibo_ = nullptr;

    // ── 边线 ──
    QOpenGLVertexArrayObject edgeVao_;
    QOpenGLBuffer edgeVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer* edgeIbo_ = nullptr;
    int edgeIndexCount_ = 0;

    // ── 拾取 ──
    QOpenGLShaderProgram* pickShader_ = nullptr;
    QOpenGLFramebufferObject* pickFbo_ = nullptr;
    std::vector<int> triToElem_;        // 三角形索引 → 单元 ID
    std::vector<int> triToFace_;        // 三角形索引 → 面序号
    PickMode pickMode_ = PickMode::Node;  // 当前拾取模式（与下拉框默认值同步）
    FESelection selection_;             // 当前选中状态
    std::unordered_set<int> selectedElements_;  // 选中的单元 ID 集合

    // 选中的面：pair<elemId, faceIndex>
    std::set<std::pair<int,int>> selectedFaces_;

    // ── 选中高亮边线 ──
    QOpenGLVertexArrayObject selEdgeVao_;
    QOpenGLBuffer selEdgeVbo_{QOpenGLBuffer::VertexBuffer};
    int selEdgeVertCount_ = 0;
    bool selectionDirty_ = false;
    int selHlMode_ = 0;   // 0=lines, 1=points

    // ── 交互状态 ──
    QPoint lastPos_;
    QPoint pressPos_;                   // 鼠标按下位置（区分点击和拖拽）
    bool isDragging_ = false;           // 是否正在拖拽旋转/平移
    bool isBoxSelecting_ = false;       // 是否正在框选
    QRubberBand* rubberBand_ = nullptr; // 框选矩形
    QPoint boxOrigin_;                  // 框选起始点
    glm::vec3 color_{0.55f, 0.75f, 0.73f};

    // ── 坐标轴指示器 ──
    QOpenGLShaderProgram* axesShader_ = nullptr;
    QOpenGLVertexArrayObject axesVao_;
    QOpenGLBuffer axesVbo_{QOpenGLBuffer::VertexBuffer};
    int axesLineCount_ = 0;
    int axesTriCount_ = 0;

    // ── 硬件信息 ──
    QString glRenderer_, glVersion_, glslVersion_, gpuVendor_;

    // ── FPS 统计 ──
    QElapsedTimer fpsTimer_;
    int frameCount_ = 0;
    float fps_ = 0.0f;
    float frameTime_ = 0.0f;
};
