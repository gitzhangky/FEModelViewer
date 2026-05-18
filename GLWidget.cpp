/**
 * @file GLWidget.cpp
 * @brief OpenGL 渲染窗口组件实现
 */

#include "GLWidget.h"
#include "GLStateGuards.h"
#include "LabelOverlay.h"
#include "NodeVertexLookup.h"
#include "PickRenderer.h"
#include "SelectionRenderer.h"
#include "Theme.h"
#include "ColorBarOverlay.h"
#include "VisibilityFilter.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QCoreApplication>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

// ============================================================
// GLSL 着色器加载（从 Qt 资源加载外部 .glsl 文件）
// ============================================================

#include <QFile>

static QByteArray loadShaderSource(const QString& resourcePath) {
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("GLWidget: failed to load shader %s", qPrintable(resourcePath));
        return {};
    }
    return f.readAll();
}

// ── 部件颜色调色板（Catppuccin Mocha）──
static const glm::vec3 kPartPalette[] = {
    {0.61f, 0.86f, 0.63f},  // green   #a6e3a1
    {0.54f, 0.71f, 0.98f},  // blue    #89b4fa
    {0.98f, 0.70f, 0.53f},  // peach   #fab387
    {0.82f, 0.62f, 0.98f},  // mauve   #cba6f7
    {0.58f, 0.89f, 0.83f},  // teal    #94e2d5
    {0.98f, 0.89f, 0.69f},  // yellow  #f9e2af
    {0.94f, 0.56f, 0.66f},  // red     #eba0ac
    {0.71f, 0.71f, 0.98f},  // lavender #b4befe
};
static const int kPartPaletteSize = static_cast<int>(sizeof(kPartPalette) / sizeof(kPartPalette[0]));

static Mesh makeClipPlanePreviewMesh(const glm::vec3& bbMin,
                                     const glm::vec3& bbMax,
                                     const glm::vec3& origin,
                                     const glm::vec3& normal) {
    Mesh mesh;

    glm::vec3 span = bbMax - bbMin;
    float maxSpan = std::max({std::abs(span.x), std::abs(span.y), std::abs(span.z), 1.0f});
    glm::vec3 mn = bbMin - glm::vec3(maxSpan * 0.03f);
    glm::vec3 mx = bbMax + glm::vec3(maxSpan * 0.03f);

    int axis = 0;
    float ax = std::abs(normal.x);
    float ay = std::abs(normal.y);
    float az = std::abs(normal.z);
    if (ay > ax && ay >= az) axis = 1;
    else if (az > ax && az > ay) axis = 2;

    glm::vec3 p0, p1, p2, p3;
    if (axis == 0) {
        float x = origin.x;
        p0 = {x, mn.y, mn.z};
        p1 = {x, mx.y, mn.z};
        p2 = {x, mx.y, mx.z};
        p3 = {x, mn.y, mx.z};
    } else if (axis == 1) {
        float y = origin.y;
        p0 = {mn.x, y, mn.z};
        p1 = {mx.x, y, mn.z};
        p2 = {mx.x, y, mx.z};
        p3 = {mn.x, y, mx.z};
    } else {
        float z = origin.z;
        p0 = {mn.x, mn.y, z};
        p1 = {mx.x, mn.y, z};
        p2 = {mx.x, mx.y, z};
        p3 = {mn.x, mx.y, z};
    }

    mesh.addFlatQuad(p0, p1, p2, p3);
    auto pushLine = [&](const glm::vec3& a, const glm::vec3& b) {
        mesh.edgeVertices.push_back(a.x);
        mesh.edgeVertices.push_back(a.y);
        mesh.edgeVertices.push_back(a.z);
        mesh.edgeVertices.push_back(b.x);
        mesh.edgeVertices.push_back(b.y);
        mesh.edgeVertices.push_back(b.z);
    };
    pushLine(p0, p1);
    pushLine(p1, p2);
    pushLine(p2, p3);
    pushLine(p3, p0);

    return mesh;
}

// ============================================================
// 构造函数 & 公有方法
// ============================================================

GLWidget::GLWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      pickRenderer_(std::make_shared<PickRenderer>(*this)),
      selectionRenderer_(std::make_shared<SelectionRenderer>(*this)),
      labelOverlay_(std::make_shared<LabelOverlay>(*this)) {
    // 设置强焦点策略，使 widget 能接收键盘事件
    setFocusPolicy(Qt::StrongFocus);

    // 创建色标覆盖层（raster 绘制，不受 GL 状态影响）
    colorBarOverlay_ = new ColorBarOverlay(this);

    // 应用退出时先于 QOpenGLContext 销毁释放 GL 对象，避免 Qt 在析构阶段报警。
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &GLWidget::cleanupGLResources,
            Qt::DirectConnection);
}

void GLWidget::setVertexColors(const std::vector<float>& colors) {
    useVertexColor_ = true;
    // 直接上传颜色到 colorVbo_，不修改主 VBO
    vao_.bind();
    {
        ScopedBufferBind bind(colorVbo_);
        colorVbo_.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(2);
    }
    vao_.release();
    update();
}

void GLWidget::setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands) {
    useVertexColor_ = true;
    scalarMin_ = minVal;
    scalarMax_ = maxVal;
    numBands_ = numBands;
    // 先完成待上传的网格，防止 paintGL 中 uploadMesh() 覆盖标量数据
    if (needsUpload_) {
        makeCurrent();
        uploadMesh();
    }
    vao_.bind();
    {
        ScopedBufferBind bindScalar(scalarVbo_);
        scalarVbo_.allocate(scalars.data(), static_cast<int>(scalars.size() * sizeof(float)));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
        glEnableVertexAttribArray(3);
    }
    // 恢复 colorVbo_ 的 attribute 2 绑定（防止 scalarVbo_ 的 bind 污染）
    {
        ScopedBufferBind bindColor(colorVbo_);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(2);
    }
    vao_.release();
    update();
}

void GLWidget::setSliceLines(const std::vector<float>& lineVertices) {
    sliceVertCount_ = static_cast<int>(lineVertices.size() / 3);
    if (sliceVertCount_ > 0) {
        makeCurrent();
        if (!sliceVao_.isCreated()) sliceVao_.create();
        if (!sliceVbo_.isCreated()) sliceVbo_.create();
        sliceVao_.bind();
        {
            ScopedBufferBind bind(sliceVbo_);
            sliceVbo_.allocate(lineVertices.data(),
                               static_cast<int>(lineVertices.size() * sizeof(float)));
            shader_->enableAttributeArray(0);
            shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
        }
        sliceVao_.release();
        doneCurrent();
    }
    update();
}

void GLWidget::clearSliceLines() {
    sliceVertCount_ = 0;
    update();
}

void GLWidget::setIsoSurfaceMesh(const Mesh& mesh) {
    isoMesh_ = mesh;
    isoIndexCount_ = static_cast<int>(mesh.indices.size());
    isoNeedsUpload_ = true;
    update();
}

void GLWidget::clearIsoSurface() {
    isoMesh_.vertices.clear();
    isoMesh_.indices.clear();
    isoIndexCount_ = 0;
    update();
}

void GLWidget::setClipPlanePreview(const glm::vec3& bbMin,
                                   const glm::vec3& bbMax,
                                   const glm::vec3& origin,
                                   const glm::vec3& normal) {
    clipPreviewMesh_ = makeClipPlanePreviewMesh(bbMin, bbMax, origin, normal);
    clipPreviewIndexCount_ = static_cast<int>(clipPreviewMesh_.indices.size());
    clipPreviewEdgeVertCount_ = static_cast<int>(clipPreviewMesh_.edgeVertices.size() / 3);
    clipPreviewVisible_ = clipPreviewIndexCount_ > 0;
    clipPreviewNeedsUpload_ = true;
    update();
}

void GLWidget::clearClipPlanePreview() {
    clipPreviewMesh_.vertices.clear();
    clipPreviewMesh_.indices.clear();
    clipPreviewMesh_.edgeVertices.clear();
    clipPreviewIndexCount_ = 0;
    clipPreviewEdgeVertCount_ = 0;
    clipPreviewVisible_ = false;
    clipPreviewNeedsUpload_ = false;
    update();
}

void GLWidget::setOverlayMesh(const Mesh& mesh) {
    overlayMesh_ = mesh;
    overlayNeedsUpload_ = true;
    update();
}

void GLWidget::setOverlayVisible(bool visible) {
    overlayVisible_ = visible;
    update();
}

void GLWidget::setUseVertexColor(bool use) {
    useVertexColor_ = use;
    if (!use) {
        // 退出云图模式：重新把部件索引写入 scalarVbo_
        needsColorUpload_ = true;
    }
}

void GLWidget::setMesh(const Mesh& mesh) {
    mesh_ = mesh;
    allTriIndices_ = mesh.indices;
    allEdgeIndices_ = mesh.edgeIndices;
    activeEdgeIndexCount_ = static_cast<int>(mesh.edgeIndices.size());
    triToPart_.clear();
    edgeToPart_.clear();
    vertexToNode_.clear();
    partVisibility_.clear();
    partColors_.clear();
    partTriangles_.clear();
    partElementIds_.clear();
    elemToPart_.clear();
    elemToNodes_.clear();
    nodeToElems_.clear();
    nodeToFirstVertex_.clear();
    renderEdgeToElems_.clear();
    renderEdgeNodeIds_.clear();
    hiddenElements_.clear();
    hiddenNodes_.clear();
    activeIndexCount_ = static_cast<int>(mesh.indices.size());
    partVisibilityDirty_ = false;
    edgeVisibilityDirty_ = false;
    needsColorUpload_ = false;
    selectionRenderer_->resetForMesh();
    needsUpload_ = true;
    update();
}

void GLWidget::setObjectColor(const glm::vec3& c) { color_ = c; update(); }

void GLWidget::fitToModel(const glm::vec3& center, float size) {
    cam_.target = center;
    cam_.distance = size * 1.5f;
    cam_.maxDist = size * 10.0f;
    cam_.minDist = size * 0.05f;
    cam_.panSensitivity = 0.001f;
    cam_.yaw = 30.0f;
    cam_.pitch = 25.0f;
    update();
}

void GLWidget::setTriangleToElementMap(const std::vector<int>& map) {
    triToElem_ = map;
    rebuildElementNodeMap();
    rebuildRenderEdgeMaps();
}

void GLWidget::setVertexToNodeMap(const std::vector<int>& map) {
    vertexToNode_ = map;
    rebuildNodeVertexLookup();
    rebuildElementNodeMap();
    rebuildRenderEdgeMaps();
    selectionRenderer_->markEdgeAdjacencyDirty();
}

void GLWidget::rebuildNodeVertexLookup() {
    nodeToFirstVertex_ = NodeVertexLookup::buildFirstVertexByNode(vertexToNode_);
}

void GLWidget::rebuildElementNodeMap() {
    elemToNodes_.clear();
    nodeToElems_.clear();

    int triCount = std::min(static_cast<int>(triToElem_.size()),
                            static_cast<int>(mesh_.indices.size() / 3));
    for (int t = 0; t < triCount; ++t) {
        int elemId = triToElem_[t];
        if (elemId < 0) continue;
        auto& elemNodes = elemToNodes_[elemId];
        for (int k = 0; k < 3; ++k) {
            unsigned int vi = mesh_.indices[t * 3 + k];
            if (vi >= vertexToNode_.size()) continue;
            int nodeId = vertexToNode_[vi];
            if (nodeId < 0) continue;
            elemNodes.insert(nodeId);
            nodeToElems_[nodeId].insert(elemId);
        }
    }

    int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                 static_cast<int>(mesh_.elemEdgeNodeIds.size()));
    for (int i = 0; i < elemEdgeCount; ++i) {
        int elemId = mesh_.elemEdgeToElement[i];
        if (elemId < 0) continue;
        auto [a, b] = mesh_.elemEdgeNodeIds[i];
        elemToNodes_[elemId].insert(a);
        elemToNodes_[elemId].insert(b);
        nodeToElems_[a].insert(elemId);
        nodeToElems_[b].insert(elemId);
    }
}

void GLWidget::rebuildRenderEdgeMaps() {
    renderEdgeNodeIds_.clear();
    renderEdgeToElems_.clear();

    int edgeCount = static_cast<int>(allEdgeIndices_.size() / 2);
    if (edgeCount <= 0) return;

    auto edgeKey = [](int a, int b) -> long long {
        int mn = std::min(a, b);
        int mx = std::max(a, b);
        return (static_cast<long long>(mn) << 32) |
               static_cast<unsigned int>(mx);
    };
    auto posKey = [](float x, float y, float z) -> std::string {
        return std::to_string(std::llround(x * 1000000.0f)) + "," +
               std::to_string(std::llround(y * 1000000.0f)) + "," +
               std::to_string(std::llround(z * 1000000.0f));
    };

    std::unordered_map<long long, std::unordered_set<int>> edgeElems;
    int triCount = std::min(static_cast<int>(triToElem_.size()),
                            static_cast<int>(mesh_.indices.size() / 3));
    for (int t = 0; t < triCount; ++t) {
        int elemId = triToElem_[t];
        if (elemId < 0) continue;
        for (int e = 0; e < 3; ++e) {
            unsigned int va = mesh_.indices[t * 3 + e];
            unsigned int vb = mesh_.indices[t * 3 + (e + 1) % 3];
            int na = (va < vertexToNode_.size()) ? vertexToNode_[va] : static_cast<int>(va);
            int nb = (vb < vertexToNode_.size()) ? vertexToNode_[vb] : static_cast<int>(vb);
            edgeElems[edgeKey(na, nb)].insert(elemId);
        }
    }

    int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                 static_cast<int>(mesh_.elemEdgeNodeIds.size()));
    for (int i = 0; i < elemEdgeCount; ++i) {
        int elemId = mesh_.elemEdgeToElement[i];
        if (elemId < 0) continue;
        auto [a, b] = mesh_.elemEdgeNodeIds[i];
        edgeElems[edgeKey(a, b)].insert(elemId);
    }

    std::unordered_map<std::string, int> posToNode;
    int meshVertCount = std::min(static_cast<int>(mesh_.vertices.size() / 6),
                                 static_cast<int>(vertexToNode_.size()));
    for (int i = 0; i < meshVertCount; ++i) {
        int nodeId = vertexToNode_[i];
        if (nodeId < 0) continue;
        posToNode.emplace(posKey(mesh_.vertices[i * 6],
                                 mesh_.vertices[i * 6 + 1],
                                 mesh_.vertices[i * 6 + 2]),
                          nodeId);
    }

    renderEdgeNodeIds_.reserve(edgeCount);
    renderEdgeToElems_.reserve(edgeCount);
    for (int e = 0; e < edgeCount; ++e) {
        unsigned int va = allEdgeIndices_[e * 2];
        unsigned int vb = allEdgeIndices_[e * 2 + 1];
        int na = static_cast<int>(va);
        int nb = static_cast<int>(vb);
        if (va * 3 + 2 < mesh_.edgeVertices.size()) {
            auto it = posToNode.find(posKey(mesh_.edgeVertices[va * 3],
                                            mesh_.edgeVertices[va * 3 + 1],
                                            mesh_.edgeVertices[va * 3 + 2]));
            if (it != posToNode.end()) na = it->second;
        }
        if (vb * 3 + 2 < mesh_.edgeVertices.size()) {
            auto it = posToNode.find(posKey(mesh_.edgeVertices[vb * 3],
                                            mesh_.edgeVertices[vb * 3 + 1],
                                            mesh_.edgeVertices[vb * 3 + 2]));
            if (it != posToNode.end()) nb = it->second;
        }
        renderEdgeNodeIds_.push_back({std::min(na, nb), std::max(na, nb)});

        std::vector<int> elems;
        auto it = edgeElems.find(edgeKey(na, nb));
        if (it != edgeElems.end())
            elems.assign(it->second.begin(), it->second.end());
        renderEdgeToElems_.push_back(std::move(elems));
    }
}

void GLWidget::markVisibilityDirty() {
    partVisibilityDirty_ = true;
    edgeVisibilityDirty_ = true;
    selectionRenderer_->markSelectionDirty();
    update();
}

bool GLWidget::isPartVisible(int partIndex) const {
    return VisibilityFilter::isPartVisible(partIndex, partVisibility_);
}

bool GLWidget::isElementVisible(int elemId) const {
    if (elemId < 0) return true;
    auto pit = elemToPart_.find(elemId);
    if (pit != elemToPart_.end() && !isPartVisible(pit->second))
        return false;
    return VisibilityFilter::isElementVisible(elemId, hiddenElements_, hiddenNodes_, elemToNodes_);
}

bool GLWidget::isElementRenderable(int elemId) const {
    return isElementVisible(elemId);
}

bool GLWidget::isTriangleVisible(int triIndex) const {
    if (triIndex < 0) return false;
    if (triIndex < static_cast<int>(triToPart_.size()) && !isPartVisible(triToPart_[triIndex]))
        return false;
    if (triIndex < static_cast<int>(triToElem_.size()))
        return isElementVisible(triToElem_[triIndex]);

    if (!hiddenNodes_.empty() && triIndex * 3 + 2 < static_cast<int>(mesh_.indices.size())) {
        for (int k = 0; k < 3; ++k) {
            unsigned int vi = mesh_.indices[triIndex * 3 + k];
            if (vi < vertexToNode_.size() && hiddenNodes_.count(vertexToNode_[vi]))
                return false;
        }
    }
    return true;
}

bool GLWidget::isNodeVisible(int nodeId) const {
    if (nodeId < 0) return false;
    if (hiddenNodes_.count(nodeId)) return false;
    auto it = nodeToElems_.find(nodeId);
    if (it == nodeToElems_.end()) return true;
    for (int elemId : it->second)
        if (isElementVisible(elemId)) return true;
    return false;
}

void GLWidget::setNodesVisibility(const std::vector<int>& nodeIds, bool visible) {
    for (int nodeId : nodeIds) {
        if (visible) hiddenNodes_.erase(nodeId);
        else hiddenNodes_.insert(nodeId);
    }
    markVisibilityDirty();
}

void GLWidget::setElementsVisibility(const std::vector<int>& elementIds, bool visible) {
    for (int elemId : elementIds) {
        if (visible) hiddenElements_.erase(elemId);
        else hiddenElements_.insert(elemId);
    }
    markVisibilityDirty();
}

int GLWidget::vertexCount()   const { return static_cast<int>(mesh_.vertices.size() / 6); }
int GLWidget::triangleCount() const { return static_cast<int>(mesh_.indices.size() / 3); }

// ============================================================
// OpenGL 生命周期回调
// ============================================================

void GLWidget::initializeGL() {
    // 初始化 OpenGL 函数指针（Qt 的 OpenGL 函数加载机制）
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed,
            this, &GLWidget::cleanupGLResources,
            Qt::DirectConnection);
    glResourcesCleaned_ = false;

    // 查询并保存 GPU 硬件信息
    glRenderer_  = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    glVersion_   = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    glslVersion_ = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    gpuVendor_   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));

    // 编译并链接着色器程序
    shader_ = new QOpenGLShaderProgram(this);
    shader_->addShaderFromSourceCode(QOpenGLShader::Vertex, loadShaderSource(":/shaders/scene.vert"));
    shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, loadShaderSource(":/shaders/scene.frag"));
    shader_->link();

    // 创建 VAO（顶点数组对象）、VBO（顶点缓冲）、IBO（索引缓冲）
    vao_.create();
    vbo_.create();
    ibo_ = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    ibo_->create();
    colorVbo_.create();
    scalarVbo_.create();

    // ── 创建 triToPart texture buffer ──
    glGenBuffers(1, &triPartTbo_);
    glGenTextures(1, &triPartTex_);

    // ── 拾取（委托给 PickRenderer） ──
    if (!pickRenderer_) pickRenderer_ = std::make_shared<PickRenderer>(*this);
    pickRenderer_->initGL();

    // ── 边线 VAO/VBO（FE 模式专用）──
    edgeVao_.create();
    edgeVbo_.create();

    // ── 选中高亮（委托给 SelectionRenderer） ──
    if (!selectionRenderer_) selectionRenderer_ = std::make_shared<SelectionRenderer>(*this);
    selectionRenderer_->initGL();

    if (!labelOverlay_) labelOverlay_ = std::make_shared<LabelOverlay>(*this);

    // ── 渐变背景着色器 + VAO/VBO（一次性创建）──
    bgShader_ = new QOpenGLShaderProgram(this);
    bgShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, loadShaderSource(":/shaders/background.vert"));
    bgShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, loadShaderSource(":/shaders/background.frag"));
    bgShader_->link();

    bgVao_.create();
    bgVbo_.create();
    {
        // 全屏四边形：pos(2) + color(3)，使用当前主题颜色
        float bgData[] = {
            -1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
             1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
             1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
            -1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
             1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
            -1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
        };
        bgVao_.bind();
        {
            ScopedBufferBind bind(bgVbo_);
            bgVbo_.allocate(bgData, sizeof(bgData));
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                                  reinterpret_cast<void*>(2 * sizeof(float)));
            glEnableVertexAttribArray(1);
        }
        bgVao_.release();
    }

    // ── 坐标轴指示器着色器 ──
    axesShader_ = new QOpenGLShaderProgram(this);
    axesShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, loadShaderSource(":/shaders/axes.vert"));
    axesShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, loadShaderSource(":/shaders/axes.frag"));
    axesShader_->link();

    // ── 生成坐标轴几何数据（实心轴杆圆柱 + 箭头圆锥 + 中心球）──
    // 每顶点 6 float: pos(3) + color(3)
    std::vector<float> lineVerts;   // GL_LINES (unused, kept for count)
    std::vector<float> triVerts;    // GL_TRIANGLES

    const int segs = 24;            // 圆柱/圆锥分段数
    const float shaftLen = 0.70f;   // 轴杆长度
    const float shaftR = 0.028f;    // 轴杆圆柱半径
    const float coneBase = 0.10f;   // 圆锥底面半径
    const float coneLen = 0.30f;    // 圆锥长度
    const float ballR = 0.065f;     // 中心球半径

    struct Axis { glm::vec3 dir; glm::vec3 up; glm::vec3 color; };
    Axis axes[] = {
        {{1,0,0}, {0,1,0}, {0.95f, 0.30f, 0.30f}},  // X 红
        {{0,1,0}, {0,0,1}, {0.35f, 0.90f, 0.35f}},   // Y 绿
        {{0,0,1}, {1,0,0}, {0.35f, 0.55f, 1.00f}},    // Z 蓝
    };

    auto pushTri = [&](glm::vec3 p, glm::vec3 c) {
        triVerts.push_back(p.x); triVerts.push_back(p.y); triVerts.push_back(p.z);
        triVerts.push_back(c.x); triVerts.push_back(c.y); triVerts.push_back(c.z);
    };

    const float PI = 3.14159265f;

    for (auto& a : axes) {
        glm::vec3 right = glm::normalize(glm::cross(a.dir, a.up));
        glm::vec3 up2 = glm::normalize(glm::cross(right, a.dir));

        // ── 实心轴杆（圆柱） ──
        for (int i = 0; i < segs; ++i) {
            float a0 = 2.0f * PI * i / segs;
            float a1 = 2.0f * PI * (i + 1) / segs;
            glm::vec3 offset0 = (right * cosf(a0) + up2 * sinf(a0)) * shaftR;
            glm::vec3 offset1 = (right * cosf(a1) + up2 * sinf(a1)) * shaftR;
            glm::vec3 b0 = offset0;                       // 底圆点
            glm::vec3 b1 = offset1;
            glm::vec3 t0 = a.dir * shaftLen + offset0;    // 顶圆点
            glm::vec3 t1 = a.dir * shaftLen + offset1;
            glm::vec3 cyl = a.color * 0.85f;
            // 两个三角形组成一个矩形面
            pushTri(b0, cyl); pushTri(t0, cyl); pushTri(t1, cyl);
            pushTri(b0, cyl); pushTri(t1, cyl); pushTri(b1, cyl);
        }

        // ── 箭头圆锥 ──
        glm::vec3 tip = a.dir * (shaftLen + coneLen);
        for (int i = 0; i < segs; ++i) {
            float a0 = 2.0f * PI * i / segs;
            float a1 = 2.0f * PI * (i + 1) / segs;
            glm::vec3 cb0 = a.dir * shaftLen + (right * cosf(a0) + up2 * sinf(a0)) * coneBase;
            glm::vec3 cb1 = a.dir * shaftLen + (right * cosf(a1) + up2 * sinf(a1)) * coneBase;
            // 侧面
            pushTri(tip, a.color);
            pushTri(cb0, a.color * 0.75f);
            pushTri(cb1, a.color * 0.75f);
            // 底面
            pushTri(a.dir * shaftLen, a.color * 0.55f);
            pushTri(cb1, a.color * 0.55f);
            pushTri(cb0, a.color * 0.55f);
        }
    }

    // ── 中心球（细分八面体 → 更圆润） ──
    glm::vec3 ballColor(0.82f, 0.82f, 0.85f);
    // 用 UV 球生成
    const int ballRings = 8;
    const int ballSectors = 12;
    for (int r = 0; r < ballRings; ++r) {
        float phi0 = PI * r / ballRings - PI / 2.0f;
        float phi1 = PI * (r + 1) / ballRings - PI / 2.0f;
        for (int s = 0; s < ballSectors; ++s) {
            float theta0 = 2.0f * PI * s / ballSectors;
            float theta1 = 2.0f * PI * (s + 1) / ballSectors;

            glm::vec3 p00(cosf(phi0) * cosf(theta0), sinf(phi0), cosf(phi0) * sinf(theta0));
            glm::vec3 p10(cosf(phi1) * cosf(theta0), sinf(phi1), cosf(phi1) * sinf(theta0));
            glm::vec3 p01(cosf(phi0) * cosf(theta1), sinf(phi0), cosf(phi0) * sinf(theta1));
            glm::vec3 p11(cosf(phi1) * cosf(theta1), sinf(phi1), cosf(phi1) * sinf(theta1));

            p00 *= ballR; p10 *= ballR; p01 *= ballR; p11 *= ballR;

            pushTri(p00, ballColor); pushTri(p10, ballColor); pushTri(p11, ballColor);
            pushTri(p00, ballColor); pushTri(p11, ballColor); pushTri(p01, ballColor);
        }
    }

    // 合并到一个 VBO: [lines | triangles]
    axesLineCount_ = static_cast<int>(lineVerts.size() / 6);
    axesTriCount_ = static_cast<int>(triVerts.size() / 6);

    std::vector<float> allData;
    allData.insert(allData.end(), lineVerts.begin(), lineVerts.end());
    allData.insert(allData.end(), triVerts.begin(), triVerts.end());

    axesVao_.create();
    axesVbo_.create();
    axesVao_.bind();
    {
        ScopedBufferBind bind(axesVbo_);
        axesVbo_.allocate(allData.data(), static_cast<int>(allData.size() * sizeof(float)));
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    axesVao_.release();

    // 启用深度测试，确保近处物体遮挡远处物体
    glEnable(GL_DEPTH_TEST);

    // 启用多重采样抗锯齿
    glEnable(GL_MULTISAMPLE);

    // 启用线段抗锯齿（边线更平滑）
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // 启动 FPS 计时器
    fpsTimer_.start();

    // 上传初始网格数据到 GPU
    uploadMesh();

    // 初始状态不显示色标（加载结果并应用后才显示）
    colorBarVisible_ = false;

    // 通知外部 GL 已初始化（MonitorPanel 会在此时读取硬件信息）
    emit glInitialized();
}

void GLWidget::cleanupGLResources() {
    if (glResourcesCleaned_ || !context()) return;

    makeCurrent();

    if (triPartTex_) {
        glDeleteTextures(1, &triPartTex_);
        triPartTex_ = 0;
    }
    if (triPartTbo_) {
        glDeleteBuffers(1, &triPartTbo_);
        triPartTbo_ = 0;
    }

    pickRenderer_.reset();
    selectionRenderer_.reset();
    labelOverlay_.reset();

    vao_.destroy();
    vbo_.destroy();
    colorVbo_.destroy();
    scalarVbo_.destroy();
    delete ibo_;
    ibo_ = nullptr;

    bgVao_.destroy();
    bgVbo_.destroy();
    edgeVao_.destroy();
    edgeVbo_.destroy();
    delete edgeIbo_;
    edgeIbo_ = nullptr;

    overlayVao_.destroy();
    overlayVbo_.destroy();
    sliceVao_.destroy();
    sliceVbo_.destroy();

    isoVao_.destroy();
    isoVbo_.destroy();
    delete isoIbo_;
    isoIbo_ = nullptr;

    clipPreviewVao_.destroy();
    clipPreviewVbo_.destroy();
    delete clipPreviewIbo_;
    clipPreviewIbo_ = nullptr;
    clipPreviewEdgeVao_.destroy();
    clipPreviewEdgeVbo_.destroy();

    axesVao_.destroy();
    axesVbo_.destroy();

    delete shader_;
    shader_ = nullptr;
    delete bgShader_;
    bgShader_ = nullptr;
    delete axesShader_;
    axesShader_ = nullptr;

    glResourcesCleaned_ = true;
    doneCurrent();
}

void GLWidget::paintGL() {
    // 恢复 GL 状态（QPainter 可能在上一帧末尾修改了 viewport/深度/混合等）
    int dpr_ = devicePixelRatio();
    glViewport(0, 0, width() * dpr_, height() * dpr_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    pickRenderer_->processDeferredPicks();

    if (needsUpload_) uploadMesh();
    rebuildPartVisibilityIbo();
    if (needsColorUpload_) uploadColors();
    if (edgeVisibilityDirty_) rebuildEdgeIbo();

    renderBackground();

    // 无数据时只绘制坐标轴（含其 X/Y/Z 文字标签）
    if (mesh_.indices.empty() && mesh_.edgeIndices.empty()) {
        drawAxesIndicator();
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            labelOverlay_->drawAxesLabels(painter);
            painter.end();
        }
        return;
    }

    // ── 计算变换矩阵 ──
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    float nearPlane = cam_.distance * 0.01f;
    float farPlane  = cam_.distance * 10.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, nearPlane, farPlane);
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view * model;
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

    // ── 设置着色器和公共 uniform ──
    shader_->bind();

    shader_->setUniformValue("uMVP", QMatrix4x4(glm::value_ptr(glm::transpose(mvp))));
    shader_->setUniformValue("uModel", QMatrix4x4(glm::value_ptr(glm::transpose(model))));

    float nm[9];
    const float* src = glm::value_ptr(normalMat);
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            nm[r * 3 + c] = src[c * 3 + r];
    shader_->setUniformValue("uNormalMat", QMatrix3x3(nm));

    shader_->setUniformValue("uLightDir", QVector3D(-0.4f, -0.7f, -0.5f));
    glm::vec3 eyePos = cam_.eye();
    shader_->setUniformValue("uViewPos", QVector3D(eyePos.x, eyePos.y, eyePos.z));
    shader_->setUniformValue("uContourMode", useVertexColor_ && colorBarVisible_);
    shader_->setUniformValue("uScalarMin", scalarMin_);
    shader_->setUniformValue("uScalarMax", scalarMax_);
    shader_->setUniformValue("uNumBands", numBands_);
    shader_->setUniformValue("uSurfaceAlpha", 1.0f);

    // 绑定 triToPart texture buffer 到纹理单元 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, triPartTex_);
    shader_->setUniformValue("uTriPartMap", 0);

    // ── 逐步渲染 ──
    renderMainMesh();
    renderMeshEdges();
    selectionRenderer_->update();
    renderOverlayMesh();
    renderClipPreview();
    renderSliceLines();
    renderIsoSurface();
    selectionRenderer_->render(*shader_);

    shader_->release();

    drawAxesIndicator();
    render2DOverlays(mvp);
    updateFpsStats();
}

// ============================================================
// paintGL 渲染子步骤
// ============================================================

void GLWidget::rebuildPartVisibilityIbo() {
    if (!partVisibilityDirty_ || allTriIndices_.empty()) return;
    partVisibilityDirty_ = false;

    auto filtered = VisibilityFilter::filterTriangles(
        allTriIndices_, triToElem_, triToPart_, vertexToNode_,
        partVisibility_, hiddenElements_, hiddenNodes_, elemToNodes_);
    activeIndexCount_ = static_cast<int>(filtered.indices.size());
    vao_.bind();
    ibo_->bind();
    ibo_->allocate(filtered.indices.data(),
                   static_cast<int>(filtered.indices.size() * sizeof(unsigned int)));
    vao_.release();

    glBindBuffer(GL_TEXTURE_BUFFER, triPartTbo_);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<int>(filtered.triParts.size() * sizeof(float)),
                 filtered.triParts.data(), GL_STATIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, triPartTex_);
    auto glTexBufferFn = reinterpret_cast<void(*)(GLenum, GLenum, GLuint)>(
        context()->getProcAddress("glTexBuffer"));
    if (glTexBufferFn) glTexBufferFn(GL_TEXTURE_BUFFER, GL_R32F, triPartTbo_);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void GLWidget::renderBackground() {
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    bgShader_->bind();
    bgVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    bgVao_.release();
    bgShader_->release();
    glEnable(GL_DEPTH_TEST);
}

void GLWidget::renderMainMesh() {
    int count = activeIndexCount_;
    const bool isoActive = isoIndexCount_ > 0;
    if (count <= 0 || isoActive) return;

    shader_->setUniformValue("uColor", QVector3D(color_.x, color_.y, color_.z));
    shader_->setUniformValue("uWireframe", false);
    shader_->setUniformValue("uUseVertexColor", useVertexColor_ || !partColors_.empty());
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    vao_.bind();
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    vao_.release();
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void GLWidget::renderMeshEdges() {
    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uUseVertexColor", false);

    int count = activeIndexCount_;
    int numTri = count / 3;
    float wireAlpha = 1.0f;
    if (numTri > 0 && count > 0) {
        float modelSize = cam_.maxDist * 0.1f;
        float avgEdgeLen = modelSize * 2.0f / std::sqrt(static_cast<float>(numTri));
        float fovFactor = height() / (2.0f * std::tan(glm::radians(22.5f)));
        float screenEdgePx = avgEdgeLen / cam_.distance * fovFactor;
        wireAlpha = glm::clamp((screenEdgePx - 3.0f) / 7.0f, 0.0f, 1.0f);
    }

    if (activeEdgeIndexCount_ > 0) {
        float lineW = (count == 0) ? 3.0f : 1.0f;
        float alpha = (count == 0) ? 1.0f : wireAlpha;
        shader_->setUniformValue("uColor", (count == 0)
            ? QVector3D(color_.x, color_.y, color_.z)
            : QVector3D(0.2f, 0.2f, 0.22f));
        shader_->setUniformValue("uWireAlpha", alpha);
        glLineWidth(lineW);
        if (alpha < 1.0f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        edgeVao_.bind();
        glDrawElements(GL_LINES, activeEdgeIndexCount_, GL_UNSIGNED_INT, nullptr);
        edgeVao_.release();
        glLineWidth(1.0f);
        if (alpha < 1.0f) glDisable(GL_BLEND);
    } else if (count > 0) {
        shader_->setUniformValue("uColor", QVector3D(0.2f, 0.2f, 0.22f));
        shader_->setUniformValue("uUseVertexColor", false);
        shader_->setUniformValue("uWireAlpha", wireAlpha);
        glLineWidth(1.0f);
        if (wireAlpha < 1.0f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        vao_.bind();
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        vao_.release();
        if (wireAlpha < 1.0f) glDisable(GL_BLEND);
    }
}

void GLWidget::renderOverlayMesh() {
    if (!overlayVisible_ || overlayMesh_.edgeVertices.empty()) return;

    if (overlayNeedsUpload_) {
        overlayNeedsUpload_ = false;
        if (!overlayVao_.isCreated()) overlayVao_.create();
        if (!overlayVbo_.isCreated()) overlayVbo_.create();
        overlayVao_.bind();
        {
            ScopedBufferBind bind(overlayVbo_);
            overlayVbo_.allocate(overlayMesh_.edgeVertices.data(),
                                static_cast<int>(overlayMesh_.edgeVertices.size() * sizeof(float)));
            shader_->enableAttributeArray(0);
            shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
        }
        overlayVao_.release();
        overlayVertCount_ = static_cast<int>(overlayMesh_.edgeVertices.size() / 3);
    }
    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uUseVertexColor", false);
    shader_->setUniformValue("uColor", QVector3D(0.5f, 0.5f, 0.5f));
    shader_->setUniformValue("uWireAlpha", 0.3f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    overlayVao_.bind();
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, overlayVertCount_);
    overlayVao_.release();
    glDisable(GL_BLEND);
}

void GLWidget::renderClipPreview() {
    if (!clipPreviewVisible_ || clipPreviewIndexCount_ <= 0) return;

    if (clipPreviewNeedsUpload_) {
        clipPreviewNeedsUpload_ = false;
        if (!clipPreviewVao_.isCreated()) clipPreviewVao_.create();
        if (!clipPreviewVbo_.isCreated()) clipPreviewVbo_.create();
        if (!clipPreviewIbo_) {
            clipPreviewIbo_ = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
            clipPreviewIbo_->create();
        }
        if (!clipPreviewEdgeVao_.isCreated()) clipPreviewEdgeVao_.create();
        if (!clipPreviewEdgeVbo_.isCreated()) clipPreviewEdgeVbo_.create();

        clipPreviewVao_.bind();
        {
            ScopedBufferBind bind(clipPreviewVbo_);
            clipPreviewVbo_.allocate(clipPreviewMesh_.vertices.data(),
                                     static_cast<int>(clipPreviewMesh_.vertices.size() * sizeof(float)));
            shader_->enableAttributeArray(0);
            shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            shader_->enableAttributeArray(1);
            shader_->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            clipPreviewIbo_->bind();
            clipPreviewIbo_->allocate(clipPreviewMesh_.indices.data(),
                                      static_cast<int>(clipPreviewMesh_.indices.size() * sizeof(unsigned int)));
        }
        clipPreviewVao_.release();

        clipPreviewEdgeVao_.bind();
        {
            ScopedBufferBind bind(clipPreviewEdgeVbo_);
            clipPreviewEdgeVbo_.allocate(clipPreviewMesh_.edgeVertices.data(),
                                         static_cast<int>(clipPreviewMesh_.edgeVertices.size() * sizeof(float)));
            shader_->enableAttributeArray(0);
            shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
            glDisableVertexAttribArray(1);
        }
        clipPreviewEdgeVao_.release();
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    shader_->setUniformValue("uContourMode", false);
    shader_->setUniformValue("uUseVertexColor", false);
    shader_->setUniformValue("uColor", QVector3D(0.95f, 0.58f, 0.20f));
    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uWireAlpha", 0.8f);
    clipPreviewEdgeVao_.bind();
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, clipPreviewEdgeVertCount_);
    glLineWidth(1.0f);
    clipPreviewEdgeVao_.release();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    shader_->setUniformValue("uSurfaceAlpha", 1.0f);
}

void GLWidget::renderSliceLines() {
    if (sliceVertCount_ <= 0) return;

    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uUseVertexColor", false);
    shader_->setUniformValue("uColor", QVector3D(1.0f, 0.2f, 0.2f));
    shader_->setUniformValue("uWireAlpha", 1.0f);
    glDisable(GL_DEPTH_TEST);
    sliceVao_.bind();
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, sliceVertCount_);
    glLineWidth(1.0f);
    sliceVao_.release();
    glEnable(GL_DEPTH_TEST);
}

void GLWidget::renderIsoSurface() {
    if (isoIndexCount_ <= 0) return;

    if (isoNeedsUpload_) {
        isoNeedsUpload_ = false;
        if (!isoVao_.isCreated()) isoVao_.create();
        if (!isoVbo_.isCreated()) isoVbo_.create();
        if (!isoIbo_) {
            isoIbo_ = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
            isoIbo_->create();
        }
        isoVao_.bind();
        {
            ScopedBufferBind bind(isoVbo_);
            isoVbo_.allocate(isoMesh_.vertices.data(),
                             static_cast<int>(isoMesh_.vertices.size() * sizeof(float)));
            shader_->enableAttributeArray(0);
            shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            shader_->enableAttributeArray(1);
            shader_->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            isoIbo_->bind();
            isoIbo_->allocate(isoMesh_.indices.data(),
                              static_cast<int>(isoMesh_.indices.size() * sizeof(unsigned int)));
        }
        isoVao_.release();
    }
    shader_->setUniformValue("uWireframe", false);
    shader_->setUniformValue("uUseVertexColor", false);
    shader_->setUniformValue("uColor", QVector3D(0.2f, 0.8f, 0.4f));
    shader_->setUniformValue("uContourMode", false);
    shader_->setUniformValue("uSurfaceAlpha", 0.75f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    shader_->setUniformValue("uWireAlpha", 0.75f);
    isoVao_.bind();
    glDrawElements(GL_TRIANGLES, isoIndexCount_, GL_UNSIGNED_INT, nullptr);
    isoVao_.release();
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    shader_->setUniformValue("uSurfaceAlpha", 1.0f);
}

void GLWidget::render2DOverlays(const glm::mat4& mvp) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    labelOverlay_->render(painter, mvp);
    painter.end();
}

void GLWidget::updateFpsStats() {
    frameCount_++;
    qint64 elapsed = fpsTimer_.elapsed();
    if (elapsed >= 500) {
        fps_ = frameCount_ * 1000.0f / elapsed;
        frameTime_ = elapsed / static_cast<float>(frameCount_);
        frameCount_ = 0;
        fpsTimer_.restart();
    }
}

void GLWidget::bindWidgetFramebuffer() {
    // QOpenGLWidget 渲染到自己的内部 FBO，必须恢复到 defaultFramebufferObject()
    // 而不是 0；后者会让后续渲染丢失。pickFbo_ 用 raw glBindFramebuffer 绑定
    // (不走 QOpenGLFramebufferObject::bind()，避免 Qt 内部 current_fbo 追踪指向
    // pickFbo_ 这个可能在 resize 时被销毁的对象)，对应这里也用 raw 调用恢复。
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
}



void GLWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);

    pickRenderer_->resizeFbo(w, h, devicePixelRatio());

    // 色标覆盖层跟随窗口大小
    if (colorBarOverlay_)
        colorBarOverlay_->resize(size());
}

// ============================================================
// 鼠标与键盘事件
// ============================================================

void GLWidget::mousePressEvent(QMouseEvent* e) {
    pressPos_ = e->pos();
    lastPos_ = e->pos();
    isDragging_ = false;
    isBoxSelecting_ = false;
    isBoxDeselecting_ = false;

    bool hasMod = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));

    if (hasMod) {
        if (e->button() == Qt::LeftButton) {
            // Ctrl/Shift + 左键 → 框选/点选（添加选中）
            isBoxSelecting_ = true;
            boxOrigin_ = e->pos();
            if (!rubberBand_)
                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
            rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
            rubberBand_->show();
        } else if (e->button() == Qt::RightButton) {
            // Ctrl/Shift + 右键 → 框选/点选（取消选中）
            isBoxDeselecting_ = true;
            boxOrigin_ = e->pos();
            if (!rubberBand_)
                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
            rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
            rubberBand_->show();
        }
    }
}

void GLWidget::mouseMoveEvent(QMouseEvent* e) {
    // 框选模式（选中/取消）：更新矩形
    if ((isBoxSelecting_ || isBoxDeselecting_) && rubberBand_) {
        rubberBand_->setGeometry(QRect(boxOrigin_, e->pos()).normalized());
        return;
    }

    float dx = e->x() - lastPos_.x();
    float dy = e->y() - lastPos_.y();
    lastPos_ = e->pos();

    // 判断是否已经开始拖拽（超过 5 像素阈值）
    if (!isDragging_) {
        QPoint diff = e->pos() - pressPos_;
        if (diff.manhattanLength() > 5)
            isDragging_ = true;
        else
            return;
    }

    // Ctrl/Shift + 左键用于拾取，不旋转
    if ((e->buttons() & Qt::LeftButton) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
        cam_.rotate(dx, dy);
    // Ctrl/Shift + 右键用于取消拾取，不平移
    if ((e->buttons() & (Qt::RightButton | Qt::MiddleButton)) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
        cam_.pan(dx, dy);

    // 部件模式轮廓边依赖视角，相机变化时需要刷新
    if (pickMode_ == PickMode::Part && selection_.hasSelection())
        selectionRenderer_->markSilhouetteDirty();

    update();
}

void GLWidget::mouseReleaseEvent(QMouseEvent* e) {
    // ── Ctrl/Shift + 左键：添加选中（点选/框选） ──
    if (e->button() == Qt::LeftButton && isBoxSelecting_ && rubberBand_) {
        rubberBand_->hide();
        isBoxSelecting_ = false;

        QRect rect = QRect(boxOrigin_, e->pos()).normalized();
        bool ctrl = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
        if (rect.width() > 3 && rect.height() > 3) {
            pickRenderer_->requestPickRect(rect, ctrl);
            update();
        } else {
            pickRenderer_->requestPickPoint(e->pos(), ctrl);
            update();
        }
    }

    // ── Ctrl/Shift + 右键：取消选中（点选/框选） ──
    if (e->button() == Qt::RightButton && isBoxDeselecting_ && rubberBand_) {
        rubberBand_->hide();
        isBoxDeselecting_ = false;

        QRect rect = QRect(boxOrigin_, e->pos()).normalized();
        if (rect.width() > 3 && rect.height() > 3) {
            pickRenderer_->requestDeselectRect(rect);
            update();
        } else {
            pickRenderer_->requestDeselectPoint(e->pos());
            update();
        }
    }

    isDragging_ = false;
}

void GLWidget::wheelEvent(QWheelEvent* e) {
    // 按住中键或右键拖动时忽略滚轮，防止平移与缩放同时触发
    if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) return;
    cam_.zoom(e->angleDelta().y() / 120.0f);
    if (pickMode_ == PickMode::Part && selection_.hasSelection())
        selectionRenderer_->markSilhouetteDirty();
    update();
}

void GLWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        if (selection_.hasSelection()) {
            selection_.clear();
            selectionRenderer_->clearHighlight();
            emit selectionChanged(pickMode_, 0, {});
            update();
        } else {
            window()->close();
        }
    } else {
        QOpenGLWidget::keyPressEvent(e);
    }
}


// ============================================================
// 私有方法
// ============================================================

void GLWidget::setPickMode(PickMode mode) {
    if (mode == pickMode_) return;
    pickMode_ = mode;

    // 切换拾取模式时清除之前的选中状态
    if (selection_.hasSelection()) {
        selection_.clear();
        selectionRenderer_->clearHighlight();
        emit selectionChanged(pickMode_, 0, {});
        if (mode == PickMode::Part)
            emit partsPicked({});
        update();
    }
}

void GLWidget::drawAxesIndicator() {
    const int axesSize = 120;
    const int margin = 8;
    const int dpr = devicePixelRatio();

    // 仅旋转的 view 矩阵（固定距离，跟随相机朝向）
    glm::mat3 rot = glm::mat3(cam_.viewMatrix());
    glm::vec3 axesEye = glm::vec3(rot[0][2], rot[1][2], rot[2][2]) * 2.5f;
    glm::mat4 axesView = glm::lookAt(axesEye, glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 axesProj = glm::ortho(-1.3f, 1.3f, -1.3f, 1.3f, 0.01f, 10.0f);
    glm::mat4 axesMVP = axesProj * axesView;

    // ── 左下角小视口绘制 ──
    glViewport(margin * dpr, margin * dpr, axesSize * dpr, axesSize * dpr);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    axesShader_->bind();
    axesShader_->setUniformValue("uMVP",
        QMatrix4x4(glm::value_ptr(glm::transpose(axesMVP))));

    axesVao_.bind();

    // 绘制实心几何体（圆柱轴杆 + 圆锥箭头 + 球心）
    if (axesLineCount_ > 0) {
        glLineWidth(2.5f);
        glDrawArrays(GL_LINES, 0, axesLineCount_);
    }
    glDrawArrays(GL_TRIANGLES, axesLineCount_, axesTriCount_);

    axesVao_.release();
    axesShader_->release();

    // 恢复主视口
    glViewport(0, 0, width() * dpr, height() * dpr);

    // 保存投影参数，供 LabelOverlay 绘制 X/Y/Z 文字。
    labelOverlay_->setAxesMvp(axesMVP);
}

void GLWidget::setShowLabels(bool show) {
    if (showLabels_ != show) {
        showLabels_ = show;
        update();
    }
}

void GLWidget::selectByIds(PickMode mode, const std::vector<int>& ids) {
    pickMode_ = mode;
    selection_.clear();

    if (mode == PickMode::Node) {
        // 过滤：只保留网格中实际存在的节点 ID
        std::unordered_set<int> validNodes(vertexToNode_.begin(), vertexToNode_.end());
        for (int id : ids) {
            if (validNodes.count(id) && isNodeVisible(id))
                selection_.selectedNodes.insert(id);
        }
    } else if (mode == PickMode::Part) {
        for (int pi : ids) {
            if (isPartVisible(pi))
                pickRenderer_->selectPart(pi);
        }
    } else {
        // 过滤：只保留渲染网格中存在的单元 ID（含三角面和 1D 边线）
        std::unordered_set<int> validElems(triToElem_.begin(), triToElem_.end());
        for (int eid : mesh_.elemEdgeToElement)
            validElems.insert(eid);
        for (int id : ids) {
            if (validElems.count(id) && isElementVisible(id))
                selection_.selectedElements.insert(id);
        }
    }

    selectionRenderer_->markSelectionDirty();

    // 发射选中变更信号（只包含实际匹配的 ID）
    std::vector<int> matchedIds;
    if (mode == PickMode::Node) {
        matchedIds.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
    } else if (mode == PickMode::Part) {
        matchedIds.assign(ids.begin(), ids.end());
    } else {
        matchedIds.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
    }
    std::sort(matchedIds.begin(), matchedIds.end());
    emit selectionChanged(mode, static_cast<int>(matchedIds.size()), matchedIds);

    if (mode == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (pickRenderer_->isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }

    update();
}

void GLWidget::setColorBarVisible(bool visible) {
    colorBarVisible_ = visible;
    if (colorBarOverlay_) {
        colorBarOverlay_->setVisible(visible);
        colorBarOverlay_->resize(size());
    }
    update();
}

void GLWidget::setColorBarRange(float min, float max) {
    colorBarMin_ = min;
    colorBarMax_ = max;
    if (colorBarOverlay_) colorBarOverlay_->setRange(min, max);
    update();
}

void GLWidget::setColorBarTitle(const QString& title) {
    colorBarTitle_ = title;
    if (colorBarOverlay_) colorBarOverlay_->setTitle(title);
    update();
}

void GLWidget::setColorBarExtremes(int minId, float minVal, int maxId, float maxVal) {
    if (colorBarOverlay_) colorBarOverlay_->setExtremes(minId, minVal, maxId, maxVal);
    update();
}

void GLWidget::setColorBarIdLabel(const QString& label) {
    if (colorBarOverlay_) colorBarOverlay_->setIdLabel(label);
    update();
}

void GLWidget::applyTheme(const Theme& theme) {
    // 更新色标文字颜色
    barTextColor_ = QColor(theme.barTextR, theme.barTextG, theme.barTextB);
    if (colorBarOverlay_) colorBarOverlay_->setTextColor(barTextColor_);

    // 存储背景颜色（initializeGL 会使用）
    bgTopColor_[0] = theme.bgTopR; bgTopColor_[1] = theme.bgTopG; bgTopColor_[2] = theme.bgTopB;
    bgBotColor_[0] = theme.bgBotR; bgBotColor_[1] = theme.bgBotG; bgBotColor_[2] = theme.bgBotB;

    // 更新渐变背景 VBO（仅在 GL 已初始化时）
    if (bgVbo_.isCreated()) {
        float bgData[] = {
            -1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
            -1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
            -1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
        };
        makeCurrent();
        {
            ScopedBufferBind bind(bgVbo_);
            bgVbo_.write(0, bgData, sizeof(bgData));
        }
        doneCurrent();
    }
    update();
}

void GLWidget::setTriangleToPartMap(const std::vector<int>& map) {
    triToPart_ = map;
    // Assign palette colors to parts
    int numParts = 0;
    for (int p : map) if (p >= 0) numParts = std::max(numParts, p + 1);
    partColors_.resize(numParts);
    for (int i = 0; i < numParts; ++i)
        partColors_[i] = kPartPalette[i % kPartPaletteSize];

    // ── 预构建每部件的三角形索引和单元 ID 列表 ──
    partTriangles_.clear();
    partTriangles_.resize(numParts);
    partElementIds_.clear();
    partElementIds_.resize(numParts);

    int triCount = static_cast<int>(map.size());
    for (int t = 0; t < triCount; ++t) {
        int p = map[t];
        if (p >= 0 && p < numParts) {
            partTriangles_[p].push_back(t);
        }
    }
    // 从 triToElem_ 提取每部件的去重单元 ID，并构建 elemToPart_ 反查表
    elemToPart_.clear();
    for (int p = 0; p < numParts; ++p) {
        std::unordered_set<int> elemSet;
        for (int t : partTriangles_[p]) {
            if (t < static_cast<int>(triToElem_.size())) {
                elemSet.insert(triToElem_[t]);
            }
        }
        partElementIds_[p].assign(elemSet.begin(), elemSet.end());
        for (int eid : partElementIds_[p]) {
            elemToPart_[eid] = p;
        }
    }
    selectionRenderer_->invalidatePartEdgeCache();
    rebuildRenderEdgeMaps();

    // 上传 triToPart 到 texture buffer（供片段着色器用 gl_PrimitiveID 查表）
    triPartDirty_ = true;
    needsColorUpload_ = true;
    update();
}

void GLWidget::setEdgeToPartMap(const std::vector<int>& map) {
    edgeToPart_ = map;
    edgeVisibilityDirty_ = true;
    rebuildRenderEdgeMaps();
    update();
}

void GLWidget::setPartVisibility(int partIndex, bool visible) {
    partVisibility_[partIndex] = visible;
    markVisibilityDirty();
}

void GLWidget::highlightParts(const std::vector<int>& partIndices) {
    // 清除当前选中
    selection_.selectedElements.clear();
    selection_.selectedNodes.clear();

    // 将指定部件的所有单元加入选中
    for (int pi : partIndices)
        pickRenderer_->selectPart(pi);

    // 触发高亮重建
    selectionRenderer_->markSelectionDirty();

    // 切换到部件拾取模式以使用轮廓边高亮
    pickMode_ = PickMode::Part;

    emit selectionChanged(pickMode_,
                          static_cast<int>(selection_.selectedElements.size()),
                          std::vector<int>(selection_.selectedElements.begin(),
                                           selection_.selectedElements.end()));
    update();
}

void GLWidget::uploadColors() {
    needsColorUpload_ = false;

    // 云图模式下颜色由片段着色器从标量值生成，不需要更新
    if (useVertexColor_) return;

    if (triToPart_.empty()) return;

    // 上传 triToPart 到 texture buffer（供片段着色器用 gl_PrimitiveID 查表）
    if (triPartDirty_) {
        triPartDirty_ = false;
        std::vector<float> triPartData(triToPart_.size());
        for (size_t i = 0; i < triToPart_.size(); ++i)
            triPartData[i] = static_cast<float>(triToPart_[i]);
        glBindBuffer(GL_TEXTURE_BUFFER, triPartTbo_);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<int>(triPartData.size() * sizeof(float)),
                     triPartData.data(), GL_STATIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, triPartTex_);
        auto glTexBufferFn = reinterpret_cast<void(*)(GLenum, GLenum, GLuint)>(
            context()->getProcAddress("glTexBuffer"));
        if (glTexBufferFn) glTexBufferFn(GL_TEXTURE_BUFFER, GL_R32F, triPartTbo_);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
}

void GLWidget::rebuildEdgeIbo() {
    edgeVisibilityDirty_ = false;
    if (allEdgeIndices_.empty()) return;

    std::vector<unsigned int> filtered = VisibilityFilter::filterEdges(
        allEdgeIndices_, edgeToPart_, renderEdgeToElems_, renderEdgeNodeIds_,
        partVisibility_, hiddenElements_, hiddenNodes_, elemToNodes_);
    activeEdgeIndexCount_ = static_cast<int>(filtered.size());

    edgeVao_.bind();
    if (!edgeIbo_) {
        edgeIbo_ = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
        edgeIbo_->create();
    }
    edgeIbo_->bind();
    edgeIbo_->allocate(filtered.data(),
                       static_cast<int>(filtered.size() * sizeof(unsigned int)));
    edgeVao_.release();
}

void GLWidget::uploadMesh() {
    needsUpload_ = false;

    vao_.bind();

    // 上传顶点数据到 VBO — 始终 6-float 步长 [pos(3) + normal(3)]
    {
        ScopedBufferBind bind(vbo_);
        vbo_.allocate(mesh_.vertices.data(),
                      static_cast<int>(mesh_.vertices.size() * sizeof(float)));

        // 上传索引数据到 IBO（IndexBuffer 是 VAO state，跟随 vao_.release 自动失效）
        activeIndexCount_ = static_cast<int>(mesh_.indices.size());
        ibo_->bind();
        ibo_->allocate(mesh_.indices.data(),
                       static_cast<int>(mesh_.indices.size() * sizeof(unsigned int)));

        // 始终 6-float 步长
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }

    // ── 颜色缓冲（per-vertex，默认为 color_） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultColors(vertCount * 3);
        for (int v = 0; v < vertCount; ++v) {
            defaultColors[v * 3 + 0] = color_.x;
            defaultColors[v * 3 + 1] = color_.y;
            defaultColors[v * 3 + 2] = color_.z;
        }
        ScopedBufferBind bind(colorVbo_);
        colorVbo_.allocate(defaultColors.data(), static_cast<int>(defaultColors.size() * sizeof(float)));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(2);
    }

    // ── 标量值缓冲（per-vertex，默认全 0） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultScalars(vertCount, 0.0f);
        ScopedBufferBind bind(scalarVbo_);
        scalarVbo_.allocate(defaultScalars.data(), static_cast<int>(defaultScalars.size() * sizeof(float)));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
        glEnableVertexAttribArray(3);
    }

    vao_.release();

    // ── 上传边线数据（如果有） ──
    edgeIndexCount_ = static_cast<int>(mesh_.edgeIndices.size());
    activeEdgeIndexCount_ = edgeIndexCount_;
    if (edgeIndexCount_ > 0) {
        if (!edgeIbo_) {
            edgeIbo_ = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
            edgeIbo_->create();
        }

        edgeVao_.bind();
        {
            ScopedBufferBind bind(edgeVbo_);
            edgeVbo_.allocate(mesh_.edgeVertices.data(),
                              static_cast<int>(mesh_.edgeVertices.size() * sizeof(float)));

            edgeIbo_->bind();
            edgeIbo_->allocate(mesh_.edgeIndices.data(),
                               static_cast<int>(mesh_.edgeIndices.size() * sizeof(unsigned int)));

            // 边线顶点只有位置（3 float），无法线
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            // 法线属性设为 0（线框模式不使用法线）
            glDisableVertexAttribArray(1);
        }
        edgeVao_.release();
    }
}
