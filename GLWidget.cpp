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
#include "SelectionIdFilter.h"
#include "Theme.h"
#include "ColorBarOverlay.h"
#include "VisibilityFilter.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QShortcut>
#include <QCoreApplication>
#include <QDebug>
#include <QSurfaceFormat>
#include <QtGlobal>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext/matrix_projection.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_set>

#ifndef GL_POINT_SPRITE_COORD_ORIGIN
#define GL_POINT_SPRITE_COORD_ORIGIN 0x8CA0
#endif
#ifndef GL_UPPER_LEFT
#define GL_UPPER_LEFT 0x8CA2
#endif

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

static bool visibilityProfileEnabled() {
    static const bool enabled = qEnvironmentVariableIntValue("FERENDER_VIS_PROFILE") != 0;
    return enabled;
}

class ScopedVisibilityTimer {
public:
    explicit ScopedVisibilityTimer(const char* label)
        : label_(label), enabled_(visibilityProfileEnabled()) {
        if (enabled_) timer_.start();
    }

    ~ScopedVisibilityTimer() {
        if (!enabled_) return;
        const double ms = timer_.nsecsElapsed() / 1000000.0;
        qInfo().noquote() << QString("[FERenderVis] %1: %2 ms")
                                  .arg(QString::fromLatin1(label_))
                                  .arg(ms, 0, 'f', 2);
    }

private:
    const char* label_;
    bool enabled_;
    QElapsedTimer timer_;
};

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

struct ElementEdgeKey {
    int elemId = -1;
    int a = -1;
    int b = -1;

    bool operator==(const ElementEdgeKey& other) const {
        return elemId == other.elemId && a == other.a && b == other.b;
    }
};

struct ElementEdgeKeyHash {
    size_t operator()(const ElementEdgeKey& key) const {
        size_t h = std::hash<int>{}(key.elemId);
        h ^= std::hash<int>{}(key.a) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(key.b) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

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
    // 请求 OpenGL 4.1 Core Profile：本引擎依赖 Core 行为（如点高亮用到的
    // gl_PointCoord 在 Core 下始终启用）。集成到未设置全局默认格式的宿主程序时，
    // 若拿到 Compatibility profile，节点高亮的球面点会因 gl_PointCoord 失效而整颗被
    // discard。此处给本 widget 单独请求 Core，保证引擎在任意宿主中行为一致。
    {
        QSurfaceFormat fmt = format();
        fmt.setVersion(4, 1);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        setFormat(fmt);
    }

    // 设置强焦点策略，使 widget 能接收键盘事件
    setFocusPolicy(Qt::StrongFocus);

    // 创建色标覆盖层（raster 绘制，不受 GL 状态影响）
    colorBarOverlay_ = new ColorBarOverlay(this);

    // 注册 Space 快捷键重置旋转中心（窗口级，避免焦点丢失导致无响应）
    auto* resetPivotShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    resetPivotShortcut->setContext(Qt::WindowShortcut);
    connect(resetPivotShortcut, &QShortcut::activated, this, [this]() {
        hasCustomPivot_ = false;
        pivotMarkerActive_ = false;
        if (hasModelCenter_) {
            cam_.target = modelCenter_;
        }
        pivotResetHintClock_.start();
        pivotResetHintActive_ = true;
        update();
    });

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
    surfaceElemEdgeVertices_.clear();
    surfaceElemEdgeToElement_.clear();
    surfaceElemEdgeNodeIds_.clear();
    surfaceCache_ = FESurfaceCache();
    hasSurfaceCache_ = false;
    surfaceRebuildDirty_ = false;
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

void GLWidget::setSurfaceCache(const FESurfaceCache& cache) {
    surfaceCache_ = cache;
    hasSurfaceCache_ = !cache.empty();
    surfaceElemEdgeVertices_.clear();
    surfaceElemEdgeToElement_.clear();
    surfaceElemEdgeNodeIds_.clear();
    if (hasSurfaceCache_) {
        buildTopologyFromCache();
        rebuildSurfaceElementEdgeCache();
    }
}

void GLWidget::buildTopologyFromCache() {
    // 拓扑映射按全模型构建（含被隐藏/内部单元），不随当前可见网格变化，
    // 保证隐藏/取消隐藏、节点隐藏、拾取、部件着色始终正确。
    elemToPart_ = surfaceCache_.elemToPart;

    int numParts = 0;
    for (const auto& [eid, p] : elemToPart_) numParts = std::max(numParts, p + 1);
    partElementIds_.assign(numParts, {});
    for (const auto& [eid, p] : elemToPart_)
        if (p >= 0 && p < numParts) partElementIds_[p].push_back(eid);

    elemToNodes_.clear();
    nodeToElems_.clear();
    for (const auto& [key, infos] : surfaceCache_.faceMap)
        for (const auto& fi : infos)
            for (int nid : fi.faceNodes) {
                elemToNodes_[fi.elemId].insert(nid);
                nodeToElems_[nid].insert(fi.elemId);
            }
    for (const auto& le : surfaceCache_.lineElems) {
        elemToNodes_[le[2]].insert(le[0]); elemToNodes_[le[2]].insert(le[1]);
        nodeToElems_[le[0]].insert(le[2]); nodeToElems_[le[1]].insert(le[2]);
    }
}

void GLWidget::rebuildSurfaceElementEdgeCache() {
    surfaceElemEdgeVertices_.clear();
    surfaceElemEdgeToElement_.clear();
    surfaceElemEdgeNodeIds_.clear();
    if (!hasSurfaceCache_) return;

    auto coord = [this](int nid) -> const glm::vec3* {
        auto it = surfaceCache_.coords.find(nid);
        return it != surfaceCache_.coords.end() ? &it->second : nullptr;
    };

    size_t approxEdges = surfaceCache_.lineElems.size();
    for (const auto& [key, infos] : surfaceCache_.faceMap)
        for (const auto& fi : infos)
            approxEdges += fi.faceNodes.size();

    surfaceElemEdgeVertices_.reserve(approxEdges * 6);
    surfaceElemEdgeToElement_.reserve(approxEdges);
    surfaceElemEdgeNodeIds_.reserve(approxEdges);

    std::unordered_set<ElementEdgeKey, ElementEdgeKeyHash> seen;
    seen.reserve(approxEdges);

    auto appendEdge = [&](int elemId, int a, int b) {
        if (elemId < 0 || a < 0 || b < 0) return;
        if (a > b) std::swap(a, b);
        ElementEdgeKey key{elemId, a, b};
        if (!seen.insert(key).second) return;

        const glm::vec3* pa = coord(a);
        const glm::vec3* pb = coord(b);
        if (!pa || !pb) return;

        surfaceElemEdgeVertices_.push_back(pa->x);
        surfaceElemEdgeVertices_.push_back(pa->y);
        surfaceElemEdgeVertices_.push_back(pa->z);
        surfaceElemEdgeVertices_.push_back(pb->x);
        surfaceElemEdgeVertices_.push_back(pb->y);
        surfaceElemEdgeVertices_.push_back(pb->z);
        surfaceElemEdgeToElement_.push_back(elemId);
        surfaceElemEdgeNodeIds_.push_back({a, b});
    };

    for (const auto& [key, infos] : surfaceCache_.faceMap) {
        for (const auto& fi : infos) {
            int n = static_cast<int>(fi.faceNodes.size());
            for (int i = 0; i < n; ++i)
                appendEdge(fi.elemId, fi.faceNodes[i], fi.faceNodes[(i + 1) % n]);
        }
    }
    for (const auto& le : surfaceCache_.lineElems)
        appendEdge(le[2], le[0], le[1]);
}

void GLWidget::rebuildSurfaceFromCache() {
    ScopedVisibilityTimer totalTimer("rebuildSurfaceFromCache total");

    FERenderData rd;
    {
        ScopedVisibilityTimer timer("buildRenderData");
        rd = FEMeshConverter::buildRenderData(
            surfaceCache_, [this](int e) { return isElementVisible(e); }, nullptr, false);
    }

    {
        ScopedVisibilityTimer timer("assign render data");
        mesh_ = rd.mesh;
        allTriIndices_  = rd.mesh.indices;
        allEdgeIndices_ = rd.mesh.edgeIndices;
        triToElem_    = rd.triangleToElement;
        vertexToNode_ = rd.vertexToNode;
        triToPart_    = rd.triangleToPart;
        edgeToPart_   = rd.edgeToPart;
    }

    {
        ScopedVisibilityTimer timer("rebuild vertex/part lookup");
        // 渲染相关映射（随网格变化），拓扑映射保持缓存构建的全模型版本
        nodeToFirstVertex_ = NodeVertexLookup::buildFirstVertexByNode(vertexToNode_);

        int numParts = static_cast<int>(partColors_.size());
        for (int p : triToPart_) if (p >= 0) numParts = std::max(numParts, p + 1);
        if (static_cast<int>(partColors_.size()) < numParts) {
            partColors_.resize(numParts);
            for (int i = 0; i < numParts; ++i)
                partColors_[i] = kPartPalette[i % kPartPaletteSize];
        }
        partTriangles_.assign(numParts, {});
        for (int t = 0; t < static_cast<int>(triToPart_.size()); ++t) {
            int p = triToPart_[t];
            if (p >= 0 && p < numParts) partTriangles_[p].push_back(t);
        }
    }

    {
        ScopedVisibilityTimer timer("rebuildRenderEdgeMaps");
        rebuildRenderEdgeMaps();
    }
    selectionRenderer_->invalidatePartEdgeCache();
    selectionRenderer_->markEdgeAdjacencyDirty();
    selectionRenderer_->markSelectionDirty();

    // 缓存模式已经按当前可见集合生成了可绘制三角形/边线，后续只需上传新数据和 triPart TBO。
    needsUpload_         = true;
    needsColorUpload_    = true;
    triPartDirty_        = true;
    partVisibilityDirty_ = false;
    edgeVisibilityDirty_ = false;
}

void GLWidget::setObjectColor(const glm::vec3& c) { color_ = c; update(); }

void GLWidget::setEdgeColor(const glm::vec3& c) { edgeColor_ = c; update(); }

void GLWidget::setEdgeWidth(float w) { edgeWidth_ = std::max(1.0f, w); update(); }

void GLWidget::setEdgeAlpha(float a) { edgeAlpha_ = std::clamp(a, 0.0f, 1.0f); update(); }

void GLWidget::setSurfaceAlpha(float a) { surfaceAlpha_ = std::clamp(a, 0.0f, 1.0f); update(); }

void GLWidget::setColormapInverted(bool inverted) {
    colormapInverted_ = inverted;
    if (colorBarOverlay_) colorBarOverlay_->setColormapInverted(inverted);
    update();
}

void GLWidget::setDisplayMode(DisplayMode mode) { displayMode_ = mode; update(); }

void GLWidget::setProjectionMode(ProjectionMode mode) { projectionMode_ = mode; update(); }

void GLWidget::setBackgroundColors(const glm::vec3& top, const glm::vec3& bottom) {
    bgTopColor_[0] = top.x;    bgTopColor_[1] = top.y;    bgTopColor_[2] = top.z;
    bgBotColor_[0] = bottom.x; bgBotColor_[1] = bottom.y; bgBotColor_[2] = bottom.z;
    uploadBackgroundVbo();
    update();
}

void GLWidget::resetBackgroundToTheme() {
    for (int i = 0; i < 3; ++i) {
        bgTopColor_[i] = themeBgTop_[i];
        bgBotColor_[i] = themeBgBot_[i];
    }
    uploadBackgroundVbo();
    update();
}

void GLWidget::uploadBackgroundVbo() {
    if (!bgVbo_.isCreated()) return;
    float bgData[] = {
        -1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
         1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
         1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
        -1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
         1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
        -1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
    };
    makeCurrent();
    {
        ScopedBufferBind bind(bgVbo_);
        bgVbo_.write(0, bgData, sizeof(bgData));
    }
    doneCurrent();
}

void GLWidget::setColormap(FERenderColormap map) {
    colormap_ = map;
    if (colorBarOverlay_) colorBarOverlay_->setColormap(static_cast<int>(map));
    update();
}

void GLWidget::setNumBands(int bands) {
    numBands_ = std::max(1, bands);
    update();
}

void GLWidget::fitToModel(const glm::vec3& center, float size) {
    cam_.target = center;
    cam_.distance = size * 1.5f;
    cam_.maxDist = size * 10.0f;
    cam_.minDist = size * 0.05f;
    cam_.panSensitivity = 0.001f;
    cam_.yaw = 30.0f;
    cam_.pitch = 25.0f;
    hasCustomPivot_ = false;
    modelCenter_ = center;
    hasModelCenter_ = true;
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

    if (static_cast<int>(mesh_.edgeNodeIds.size()) >= edgeCount) {
        renderEdgeNodeIds_.reserve(edgeCount);
        renderEdgeToElems_.reserve(edgeCount);
        for (int e = 0; e < edgeCount; ++e) {
            auto [na, nb] = mesh_.edgeNodeIds[e];
            if (na > nb) std::swap(na, nb);
            renderEdgeNodeIds_.push_back({na, nb});

            std::vector<int> elems;
            if (e < static_cast<int>(mesh_.edgeToElement.size()) && mesh_.edgeToElement[e] >= 0)
                elems.push_back(mesh_.edgeToElement[e]);
            renderEdgeToElems_.push_back(std::move(elems));
        }
        return;
    }

    auto posKey = [](float x, float y, float z) -> std::string {
        return std::to_string(std::llround(x * 1000000.0f)) + "," +
               std::to_string(std::llround(y * 1000000.0f)) + "," +
               std::to_string(std::llround(z * 1000000.0f));
    };

    // 位置 → 节点ID，仅用于按"隐藏节点"过滤边线
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

        // 直接采用转换层给出的"边线→单元"映射（与 edgeIndices 同步生成，权威可靠）。
        // 旧实现靠位置反查重建该映射，匹配失败时映射为空，filterEdges 会跳过单元
        // 可见性判断，导致隐藏单元后其边线/线框仍然残留。
        std::vector<int> elems;
        if (e < static_cast<int>(mesh_.edgeToElement.size()) && mesh_.edgeToElement[e] >= 0)
            elems.push_back(mesh_.edgeToElement[e]);
        renderEdgeToElems_.push_back(std::move(elems));
    }
}

void GLWidget::markVisibilityDirty() {
    if (hasSurfaceCache_) {
        // 缓存模式：重建当前可见集合的边界面（含切口内壁）
        surfaceRebuildDirty_ = true;
    } else {
        partVisibilityDirty_ = true;
        edgeVisibilityDirty_ = true;
    }
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

void GLWidget::showAll() {
    if (hiddenElements_.empty() && hiddenNodes_.empty()) return;
    hiddenElements_.clear();
    hiddenNodes_.clear();
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

    auto glPointParameteriFn = reinterpret_cast<void(*)(GLenum, GLint)>(
        context()->getProcAddress("glPointParameteri"));
    if (glPointParameteriFn)
        glPointParameteriFn(GL_POINT_SPRITE_COORD_ORIGIN, GL_UPPER_LEFT);

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

    // 查询驱动支持的最大线宽，后续所有 glLineWidth 走 setLineWidthClamped 钳制，
    // 避免在严格 Core Profile（如 macOS，仅支持 1.0）上请求非法线宽。
    // 注意：Core Profile 下 GL_ALIASED_LINE_WIDTH_RANGE 常被驱动报告为 [1,1]，
    // 但此处已启用 GL_LINE_SMOOTH，实际生效的是抗锯齿线宽范围
    // GL_SMOOTH_LINE_WIDTH_RANGE（多数 Windows 驱动支持到 ~10）。两者取较大者，
    // 否则 Windows 上高亮线会被误钳到 1px 而显得过细。
    {
#ifndef GL_SMOOTH_LINE_WIDTH_RANGE
#define GL_SMOOTH_LINE_WIDTH_RANGE 0x0B22
#endif
        GLfloat aliased[2] = {1.0f, 1.0f};
        glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, aliased);
        GLfloat smooth[2] = {1.0f, 1.0f};
        glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE, smooth);
        maxLineWidth_ = std::max({1.0f, aliased[1], smooth[1]});
    }

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

    if (depthResolveFbo_) { glDeleteFramebuffers(1, &depthResolveFbo_); depthResolveFbo_ = 0; }
    if (depthResolveTex_) { glDeleteTextures(1, &depthResolveTex_); depthResolveTex_ = 0; }

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

    if (hasSurfaceCache_ && surfaceRebuildDirty_) {
        ScopedVisibilityTimer timer("paintGL visibility rebuild phase");
        rebuildSurfaceFromCache();
        surfaceRebuildDirty_ = false;
    }
    if (needsUpload_) {
        ScopedVisibilityTimer timer("uploadMesh");
        uploadMesh();
    }
    if (partVisibilityDirty_) {
        ScopedVisibilityTimer timer("rebuildPartVisibilityIbo");
        rebuildPartVisibilityIbo();
    }
    if (needsColorUpload_) {
        ScopedVisibilityTimer timer("uploadColors");
        uploadColors();
    }
    if (edgeVisibilityDirty_) {
        ScopedVisibilityTimer timer("rebuildEdgeIbo");
        rebuildEdgeIbo();
    }

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
    glm::mat4 projection = projectionMatrix(aspect, nearPlane, farPlane);
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
    shader_->setUniformValue("uPointHighlight", false);
    shader_->setUniformValue("uPointSize", 1.0f);
    shader_->setUniformValue("uViewportPx", QVector2D(width() * dpr_, height() * dpr_));
    shader_->setUniformValue("uScreenOffsetPx", QVector2D(0.0f, 0.0f));
    shader_->setUniformValue("uContourMode", useVertexColor_ && colorBarVisible_);
    shader_->setUniformValue("uScalarMin", scalarMin_);
    shader_->setUniformValue("uScalarMax", scalarMax_);
    shader_->setUniformValue("uNumBands", numBands_);
    shader_->setUniformValue("uColormap", static_cast<int>(colormap_));
    shader_->setUniformValue("uColormapInvert", colormapInverted_);
    shader_->setUniformValue("uSurfaceAlpha", surfaceAlpha_);

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

    // ── 延迟处理双击设置旋转中心 ──
    if (pivotPending_) {
        pivotPending_ = false;
        int dpr = devicePixelRatio();
        int fbW = width() * dpr, fbH = height() * dpr;

        // 懒创建 / 重建深度专用非 MSAA FBO
        if (depthResolveFbo_ == 0 || depthResolveW_ != fbW || depthResolveH_ != fbH) {
            if (depthResolveFbo_) { glDeleteFramebuffers(1, &depthResolveFbo_); glDeleteTextures(1, &depthResolveTex_); }
            glGenFramebuffers(1, &depthResolveFbo_);
            glGenTextures(1, &depthResolveTex_);
            glBindTexture(GL_TEXTURE_2D, depthResolveTex_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, fbW, fbH, 0,
                         GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, depthResolveFbo_);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthResolveTex_, 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            depthResolveW_ = fbW;
            depthResolveH_ = fbH;
        }

        // 渲染深度到非 MSAA FBO（轻量级：仅主网格，无颜色写入）
        glBindFramebuffer(GL_FRAMEBUFFER, depthResolveFbo_);
        glViewport(0, 0, fbW, fbH);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        shader_->bind();
        shader_->setUniformValue("uMVP", QMatrix4x4(glm::value_ptr(glm::transpose(mvp))));
        vao_.bind();
        glDrawElements(GL_TRIANGLES, activeIndexCount_, GL_UNSIGNED_INT, nullptr);
        vao_.release();
        shader_->release();

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // 读深度
        int px = std::clamp(pendingPivotPos_.x() * dpr, 0, std::max(0, fbW - 1));
        int py = std::clamp((height() - pendingPivotPos_.y()) * dpr, 0, std::max(0, fbH - 1));
        float depth = 1.0f;
        glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

        // 恢复 widget FBO 和 viewport
        bindWidgetFramebuffer();
        glViewport(0, 0, fbW, fbH);

        glm::vec3 newTarget;
        glm::vec4 vp(0, 0, fbW, fbH);
        if (depth > 0.0f && depth < 1.0f) {
            // 点击到模型上：直接 unproject
            newTarget = glm::unProject(glm::vec3(px, py, depth), view, projection, vp);
        } else {
            // 点击到背景：沿点击射线投射到与当前 target 同深度的平面
            glm::vec3 nearPt = glm::unProject(glm::vec3(px, py, 0.0f), view, projection, vp);
            glm::vec3 farPt  = glm::unProject(glm::vec3(px, py, 1.0f), view, projection, vp);
            glm::vec3 ray = glm::normalize(farPt - nearPt);
            glm::vec3 eyePos = cam_.eye();
            glm::vec3 viewDir = glm::normalize(cam_.target - eyePos);
            // 射线与经过当前 target 且法线为 viewDir 的平面求交
            float denom = glm::dot(ray, viewDir);
            if (std::abs(denom) < 1e-6f) goto skipPivot;
            float t = glm::dot(cam_.target - nearPt, viewDir) / denom;
            newTarget = nearPt + ray * t;
        }

        {
            orbitPivot_ = newTarget;
            hasCustomPivot_ = true;

            pivotMarkerPos_ = newTarget;
            pivotMarkerActive_ = true;
            pivotMarkerClock_.start();
            if (!pivotFadeTimer_) {
                pivotFadeTimer_ = new QTimer(this);
                pivotFadeTimer_->setInterval(16);
                connect(pivotFadeTimer_, &QTimer::timeout, this, [this]() {
                    if (pivotMarkerClock_.elapsed() > 2000) {
                        pivotMarkerActive_ = false;
                        pivotFadeTimer_->stop();
                    }
                    update();
                });
            }
            pivotFadeTimer_->start();
        }
        skipPivot:;
    }

    drawAxesIndicator();
    render2DOverlays(mvp);
    updateFpsStats();
}

// ============================================================
// paintGL 渲染子步骤
// ============================================================

void GLWidget::setLineWidthClamped(float width) {
    glLineWidth(std::clamp(width, 1.0f, maxLineWidth_));
}

glm::mat4 GLWidget::projectionMatrix(float aspect, float nearPlane, float farPlane) const {
    if (projectionMode_ == ProjectionMode::Orthographic) {
        // 正交：用目标距离处透视视锥的半高确定范围，使两种投影下模型大小相当
        float halfH = cam_.distance * std::tan(glm::radians(22.5f));
        float halfW = halfH * aspect;
        return glm::ortho(-halfW, halfW, -halfH, halfH, nearPlane, farPlane);
    }
    return glm::perspective(glm::radians(45.0f), aspect, nearPlane, farPlane);
}

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
    if (displayMode_ == DisplayMode::Wireframe) return;  // 线框模式不画实体面

    shader_->setUniformValue("uColor", QVector3D(color_.x, color_.y, color_.z));
    shader_->setUniformValue("uWireframe", false);
    shader_->setUniformValue("uUseVertexColor", useVertexColor_ || !partColors_.empty());
    const bool translucent = (surfaceAlpha_ < 0.999f);
    if (translucent) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    vao_.bind();
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    vao_.release();
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (translucent) glDisable(GL_BLEND);
}

void GLWidget::renderMeshEdges() {
    if (displayMode_ == DisplayMode::Solid) return;  // 实体模式不画边线
    const bool wireOnly = (displayMode_ == DisplayMode::Wireframe);

    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uUseVertexColor", false);

    int count = activeIndexCount_;
    int numTri = count / 3;
    // 实体+线框模式下，边线随屏幕边长自适应淡入淡出避免糊成一团；
    // 纯线框模式下始终不透明，保证线框完整可见。
    float wireAlpha = 1.0f;
    if (!wireOnly && numTri > 0 && count > 0) {
        float modelSize = cam_.maxDist * 0.1f;
        float avgEdgeLen = modelSize * 2.0f / std::sqrt(static_cast<float>(numTri));
        float fovFactor = height() / (2.0f * std::tan(glm::radians(22.5f)));
        float screenEdgePx = avgEdgeLen / cam_.distance * fovFactor;
        wireAlpha = glm::clamp((screenEdgePx - 3.0f) / 7.0f, 0.0f, 1.0f);
    }

    const QVector3D edgeQc(edgeColor_.x, edgeColor_.y, edgeColor_.z);

    if (activeEdgeIndexCount_ > 0) {
        // 无实体面（纯边线数据）或纯线框模式：不透明粗线；否则细线 + 自适应透明。
        // 边线宽度/不透明度由用户控制（edgeWidth_ / edgeAlpha_）。
        bool emphasize = (count == 0) || wireOnly;
        float lineW = emphasize ? std::max(edgeWidth_, 3.0f) : edgeWidth_;
        float alpha = (emphasize ? 1.0f : wireAlpha) * edgeAlpha_;
        shader_->setUniformValue("uColor", (count == 0)
            ? QVector3D(color_.x, color_.y, color_.z)
            : edgeQc);
        shader_->setUniformValue("uWireAlpha", alpha);
        setLineWidthClamped(lineW);
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
        float alpha = (wireOnly ? 1.0f : wireAlpha) * edgeAlpha_;
        shader_->setUniformValue("uColor", edgeQc);
        shader_->setUniformValue("uUseVertexColor", false);
        shader_->setUniformValue("uWireAlpha", alpha);
        setLineWidthClamped(edgeWidth_);
        if (alpha < 1.0f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        vao_.bind();
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        vao_.release();
        glLineWidth(1.0f);
        if (alpha < 1.0f) glDisable(GL_BLEND);
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
    setLineWidthClamped(2.0f);
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
    setLineWidthClamped(2.0f);
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

    // 旋转中心标记（双击设置后淡出）
    if (pivotMarkerActive_) {
        glm::vec4 clip = mvp * glm::vec4(pivotMarkerPos_, 1.0f);
        if (clip.w > 0.0f) {
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            float sx = (ndc.x * 0.5f + 0.5f) * width();
            float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * height();

            float elapsed = pivotMarkerClock_.elapsed();
            float t = elapsed / 2000.0f;
            if (t > 1.0f) t = 1.0f;
            // 前 800ms 保持不透明，之后淡出
            float fadeT = (elapsed < 800) ? 0.0f : (elapsed - 800) / 1200.0f;
            if (fadeT > 1.0f) fadeT = 1.0f;
            int alpha = static_cast<int>(255 * (1.0f - fadeT));

            // 外圈扩散
            float radius = 10.0f + 16.0f * t;
            QPen pen(QColor(255, 100, 30, alpha), 2.5f);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(sx, sy), radius, radius);

            // 内圈实心圆点
            int dotAlpha = static_cast<int>(220 * (1.0f - fadeT));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 140, 50, dotAlpha));
            painter.drawEllipse(QPointF(sx, sy), 4.0, 4.0);

            // 十字线
            float cross = 8.0f;
            painter.setPen(QPen(QColor(255, 100, 30, alpha), 2.0f));
            painter.drawLine(QPointF(sx - cross, sy), QPointF(sx + cross, sy));
            painter.drawLine(QPointF(sx, sy - cross), QPointF(sx, sy + cross));

            // 文字提示
            if (elapsed < 1400) {
                QFont font = painter.font();
                font.setPixelSize(11);
                font.setBold(true);
                painter.setFont(font);
                painter.setPen(QColor(255, 200, 150, alpha));
                painter.drawText(QPointF(sx + 14, sy - 10), "旋转中心");
            }
        }
    }

    if (pivotResetHintActive_) {
        float elapsed = pivotResetHintClock_.elapsed();
        if (elapsed > 1000) {
            pivotResetHintActive_ = false;
        } else {
            float fadeT = (elapsed < 500) ? 0.0f : (elapsed - 500) / 500.0f;
            int alpha = static_cast<int>(200 * (1.0f - fadeT));
            QFont font = painter.font();
            font.setPixelSize(13);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor(200, 200, 200, alpha));
            QString text = QStringLiteral("已重置旋转中心");
            QFontMetrics fm(font);
            int tw = fm.horizontalAdvance(text);
            painter.drawText(QPointF((width() - tw) / 2.0, height() / 2.0), text);
            update();
        }
    }

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
    setFocus();
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
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        if (hasCustomPivot_)
            orbitAroundPivot(dx, dy);
        else
            cam_.rotate(dx, dy);
    }
    // Ctrl/Shift + 右键用于取消拾取，不平移
    if ((e->buttons() & (Qt::RightButton | Qt::MiddleButton)) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        glm::vec3 oldTarget = cam_.target;
        cam_.pan(dx, dy);
        if (hasCustomPivot_)
            orbitPivot_ += (cam_.target - oldTarget);
    }

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

void GLWidget::orbitAroundPivot(float dx, float dy)
{
    float cp = cos(glm::radians(cam_.pitch));
    float yawSign = (cp >= 0.0f) ? 1.0f : -1.0f;
    float dYaw   = -dx * cam_.rotateSensitivity * yawSign;
    float dPitch = -dy * cam_.rotateSensitivity;

    glm::mat4 viewMat = cam_.viewMatrix();
    glm::vec3 right(viewMat[0][0], viewMat[1][0], viewMat[2][0]);

    glm::mat4 rotYaw = glm::rotate(glm::mat4(1.0f),
                                     glm::radians(dYaw), glm::vec3(0, 1, 0));
    glm::mat4 rotPitch = glm::rotate(glm::mat4(1.0f),
                                       glm::radians(dPitch), right);
    glm::mat4 rot = rotPitch * rotYaw;

    glm::vec3 newTarget = glm::vec3(rot * glm::vec4(cam_.target - orbitPivot_, 0.0f)) + orbitPivot_;

    cam_.rotate(dx, dy);
    cam_.target = newTarget;
}

void GLWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    if (mesh_.indices.empty()) return;

    pendingPivotPos_ = e->pos();
    pivotPending_ = true;
    update();
}

void GLWidget::wheelEvent(QWheelEvent* e) {
    // 按住中键或右键拖动时忽略滚轮，防止平移与缩放同时触发
    if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) return;

    // 鼠标位置作为缩放焦点：保持鼠标下的世界点不动
    // 通过射线-平面相交求出鼠标当前对应的世界点 P，然后以 P 为中心按缩放比例 f 缩放
    // target/eye 都会被 P + f*(X - P) 拉近/推远，方向不变（yaw/pitch 保持）
    int dpr = devicePixelRatio();
    int fbW = width() * dpr, fbH = height() * dpr;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QPointF wheelPos = e->position();
#else
    const QPointF wheelPos = e->pos();
#endif
    int px = wheelPos.x() * dpr;
    int py = (height() - wheelPos.y()) * dpr;

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    float nearPlane = cam_.distance * 0.01f;
    float farPlane  = cam_.distance * 10.0f;
    glm::mat4 projection = projectionMatrix(aspect, nearPlane, farPlane);
    glm::mat4 view = cam_.viewMatrix();
    glm::vec4 vp(0, 0, fbW, fbH);

    glm::vec3 nearPt = glm::unProject(glm::vec3(px, py, 0.0f), view, projection, vp);
    glm::vec3 farPt  = glm::unProject(glm::vec3(px, py, 1.0f), view, projection, vp);
    glm::vec3 ray = glm::normalize(farPt - nearPt);
    glm::vec3 viewDir = glm::normalize(cam_.target - cam_.eye());
    float denom = glm::dot(ray, viewDir);

    float oldDist = cam_.distance;
    cam_.zoom(e->angleDelta().y() / 120.0f);

    if (std::abs(denom) > 1e-6f && oldDist > 1e-6f) {
        float t = glm::dot(cam_.target - nearPt, viewDir) / denom;
        glm::vec3 mouseWorld = nearPt + ray * t;
        float f = cam_.distance / oldDist;
        cam_.target = mouseWorld + f * (cam_.target - mouseWorld);
        if (hasCustomPivot_) {
            orbitPivot_ = mouseWorld + f * (orbitPivot_ - mouseWorld);
        }
    }

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
        setLineWidthClamped(2.5f);
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
        // 过滤：只保留当前渲染数据中能显示/拾取到的节点 ID
        auto validNodes = SelectionIdFilter::buildSelectableNodes(vertexToNode_, renderEdgeNodeIds_);
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
        // 过滤：只保留实际渲染出的单元。完整单元边线包含内部单元，
        // 不能作为可拾取依据，否则 set 选中会出现不可点击取消的内部单元。
        auto validElems = SelectionIdFilter::buildSelectableElements(triToElem_, mesh_.edgeToElement);
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

    // 存储背景颜色（initializeGL 会使用），并记下主题预设供"恢复主题"使用
    bgTopColor_[0] = theme.bgTopR; bgTopColor_[1] = theme.bgTopG; bgTopColor_[2] = theme.bgTopB;
    bgBotColor_[0] = theme.bgBotR; bgBotColor_[1] = theme.bgBotG; bgBotColor_[2] = theme.bgBotB;
    themeBgTop_[0] = theme.bgTopR; themeBgTop_[1] = theme.bgTopG; themeBgTop_[2] = theme.bgTopB;
    themeBgBot_[0] = theme.bgBotR; themeBgBot_[1] = theme.bgBotG; themeBgBot_[2] = theme.bgBotB;

    // 更新渐变背景 VBO（仅在 GL 已初始化时）
    uploadBackgroundVbo();
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
