/**
 * @file GLWidget.cpp
 * @brief OpenGL 渲染窗口组件实现
 */

#include "GLWidget.h"
#include "Theme.h"
#include "ColorBarOverlay.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFontMetrics>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <map>

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

GLWidget::GLWidget(QWidget* parent) : QOpenGLWidget(parent) {
    // 设置强焦点策略，使 widget 能接收键盘事件
    setFocusPolicy(Qt::StrongFocus);

    // 创建色标覆盖层（raster 绘制，不受 GL 状态影响）
    colorBarOverlay_ = new ColorBarOverlay(this);
}

void GLWidget::setVertexColors(const std::vector<float>& colors) {
    useVertexColor_ = true;
    // 直接上传颜色到 colorVbo_，不修改主 VBO
    vao_.bind();
    colorVbo_.bind();
    colorVbo_.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(2);
    colorVbo_.release();
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
    scalarVbo_.bind();
    scalarVbo_.allocate(scalars.data(), static_cast<int>(scalars.size() * sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glEnableVertexAttribArray(3);
    // 恢复 colorVbo_ 的 attribute 2 绑定（防止 scalarVbo_ 的 bind 污染）
    colorVbo_.bind();
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(2);
    colorVbo_.release();
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
        sliceVbo_.bind();
        sliceVbo_.allocate(lineVertices.data(),
                           static_cast<int>(lineVertices.size() * sizeof(float)));
        shader_->enableAttributeArray(0);
        shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
        sliceVbo_.release();
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
    activeIndexCount_ = static_cast<int>(mesh.indices.size());
    partVisibilityDirty_ = false;
    edgeVisibilityDirty_ = false;
    needsColorUpload_ = false;
    edgeAdjDirty_ = true;
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
}

void GLWidget::setVertexToNodeMap(const std::vector<int>& map) {
    vertexToNode_ = map;
    edgeAdjDirty_ = true;
}

int GLWidget::vertexCount()   const { return static_cast<int>(mesh_.vertices.size() / 6); }
int GLWidget::triangleCount() const { return static_cast<int>(mesh_.indices.size() / 3); }

// ============================================================
// OpenGL 生命周期回调
// ============================================================

void GLWidget::initializeGL() {
    // 初始化 OpenGL 函数指针（Qt 的 OpenGL 函数加载机制）
    initializeOpenGLFunctions();

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

    // ── 拾取着色器 ──
    pickShader_ = new QOpenGLShaderProgram(this);
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, loadShaderSource(":/shaders/pick.vert"));
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, loadShaderSource(":/shaders/pick.frag"));
    pickShader_->link();

    // ── 拾取专用 VAO（避免共用 vao_ 导致顶点属性状态泄漏到 QPainter） ──
    pickVao_.create();

    // ── 边线 VAO/VBO（FE 模式专用）──
    edgeVao_.create();
    edgeVbo_.create();

    // ── 选中高亮边线 VAO/VBO ──
    selEdgeVao_.create();
    selEdgeVbo_.create();

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
        bgVbo_.bind();
        bgVbo_.allocate(bgData, sizeof(bgData));
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        bgVbo_.release();
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
    axesVbo_.bind();
    axesVbo_.allocate(allData.data(), static_cast<int>(allData.size() * sizeof(float)));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    axesVbo_.release();
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

void GLWidget::paintGL() {
    // 恢复 GL 状态（QPainter 可能在上一帧末尾修改了 viewport/深度/混合等）
    int dpr_ = devicePixelRatio();
    glViewport(0, 0, width() * dpr_, height() * dpr_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    processDeferredPicks();

    if (needsUpload_) uploadMesh();
    rebuildPartVisibilityIbo();
    if (needsColorUpload_) uploadColors();
    if (edgeVisibilityDirty_) rebuildEdgeIbo();

    renderBackground();

    // 无数据时只绘制坐标轴
    if (mesh_.indices.empty() && mesh_.edgeIndices.empty()) {
        drawAxesIndicator();
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
    updateSelectionHighlight();
    renderOverlayMesh();
    renderClipPreview();
    renderSliceLines();
    renderIsoSurface();
    renderSelectionHighlight();

    shader_->release();

    drawAxesIndicator();
    render2DOverlays(mvp);
    updateFpsStats();
}

// ============================================================
// paintGL 渲染子步骤
// ============================================================

void GLWidget::processDeferredPicks() {
    if (pickPointPending_) {
        pickPointPending_ = false;
        pickAtPoint(pendingPickPos_, pendingPickCtrl_);
    }
    if (pickRectPending_) {
        pickRectPending_ = false;
        pickInRect(pendingPickRect_, pendingPickCtrl_);
    }
    if (deselectPointPending_) {
        deselectPointPending_ = false;
        deselectAtPoint(pendingDeselectPos_);
    }
    if (deselectRectPending_) {
        deselectRectPending_ = false;
        deselectInRect(pendingDeselectRect_);
    }
}

void GLWidget::rebuildPartVisibilityIbo() {
    if (!partVisibilityDirty_ || allTriIndices_.empty()) return;
    partVisibilityDirty_ = false;

    std::vector<unsigned int> filtered;
    filtered.reserve(allTriIndices_.size());
    std::vector<float> filteredTriPart;
    int triCount = static_cast<int>(allTriIndices_.size() / 3);
    for (int t = 0; t < triCount; ++t) {
        int part = (t < static_cast<int>(triToPart_.size())) ? triToPart_[t] : -1;
        if (part >= 0) {
            auto it = partVisibility_.find(part);
            if (it != partVisibility_.end() && !it->second)
                continue;
        }
        filtered.push_back(allTriIndices_[t * 3]);
        filtered.push_back(allTriIndices_[t * 3 + 1]);
        filtered.push_back(allTriIndices_[t * 3 + 2]);
        filteredTriPart.push_back(static_cast<float>(part));
    }
    activeIndexCount_ = static_cast<int>(filtered.size());
    vao_.bind();
    ibo_->bind();
    ibo_->allocate(filtered.data(),
                   static_cast<int>(filtered.size() * sizeof(unsigned int)));
    vao_.release();

    glBindBuffer(GL_TEXTURE_BUFFER, triPartTbo_);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<int>(filteredTriPart.size() * sizeof(float)),
                 filteredTriPart.data(), GL_STATIC_DRAW);
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

void GLWidget::updateSelectionHighlight() {
    if (selectionDirty_) {
        std::vector<float> hlVerts;
        int hlMode = 0;

        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            partEdgeCacheValid_ = false;
            rebuildSelectionEdges();
            hlMode = 0;
        } else if (!selection_.selectedNodes.empty()) {
            std::unordered_map<int, int> nodeToFirstVertex;
            if (!vertexToNode_.empty()) {
                for (int i = 0; i < static_cast<int>(vertexToNode_.size()); ++i) {
                    int nid = vertexToNode_[i];
                    if (nid >= 0 && nodeToFirstVertex.find(nid) == nodeToFirstVertex.end())
                        nodeToFirstVertex[nid] = i;
                }
            }
            for (int nid : selection_.selectedNodes) {
                int vi = -1;
                if (!nodeToFirstVertex.empty()) {
                    auto it = nodeToFirstVertex.find(nid);
                    if (it != nodeToFirstVertex.end()) vi = it->second;
                } else {
                    vi = nid;
                }
                if (vi >= 0 && vi * 6 + 2 < static_cast<int>(mesh_.vertices.size())) {
                    hlVerts.push_back(mesh_.vertices[vi * 6]);
                    hlVerts.push_back(mesh_.vertices[vi * 6 + 1]);
                    hlVerts.push_back(mesh_.vertices[vi * 6 + 2]);
                }
            }
            selEdgeVertCount_ = static_cast<int>(hlVerts.size() / 3);
            selEdgeVao_.bind();
            selEdgeVbo_.bind();
            if (!hlVerts.empty())
                selEdgeVbo_.allocate(hlVerts.data(), static_cast<int>(hlVerts.size() * sizeof(float)));
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
            selEdgeVao_.release();
            selEdgeVbo_.release();
            hlMode = 1;
        }
        selectionDirty_ = false;
        silhouetteDirty_ = false;
        selHlMode_ = hlMode;
    } else if (silhouetteDirty_ && partEdgeCacheValid_ &&
               pickMode_ == PickMode::Part && selection_.hasSelection()) {
        updateSilhouetteFromCache();
        silhouetteDirty_ = false;
    }
}

void GLWidget::renderOverlayMesh() {
    if (!overlayVisible_ || overlayMesh_.edgeVertices.empty()) return;

    if (overlayNeedsUpload_) {
        overlayNeedsUpload_ = false;
        if (!overlayVao_.isCreated()) overlayVao_.create();
        if (!overlayVbo_.isCreated()) overlayVbo_.create();
        overlayVao_.bind();
        overlayVbo_.bind();
        overlayVbo_.allocate(overlayMesh_.edgeVertices.data(),
                            static_cast<int>(overlayMesh_.edgeVertices.size() * sizeof(float)));
        shader_->enableAttributeArray(0);
        shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
        overlayVbo_.release();
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
        clipPreviewVbo_.bind();
        clipPreviewVbo_.allocate(clipPreviewMesh_.vertices.data(),
                                 static_cast<int>(clipPreviewMesh_.vertices.size() * sizeof(float)));
        shader_->enableAttributeArray(0);
        shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
        shader_->enableAttributeArray(1);
        shader_->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
        clipPreviewIbo_->bind();
        clipPreviewIbo_->allocate(clipPreviewMesh_.indices.data(),
                                  static_cast<int>(clipPreviewMesh_.indices.size() * sizeof(unsigned int)));
        clipPreviewVbo_.release();
        clipPreviewVao_.release();

        clipPreviewEdgeVao_.bind();
        clipPreviewEdgeVbo_.bind();
        clipPreviewEdgeVbo_.allocate(clipPreviewMesh_.edgeVertices.data(),
                                     static_cast<int>(clipPreviewMesh_.edgeVertices.size() * sizeof(float)));
        shader_->enableAttributeArray(0);
        shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
        glDisableVertexAttribArray(1);
        clipPreviewEdgeVbo_.release();
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
        isoVbo_.bind();
        isoVbo_.allocate(isoMesh_.vertices.data(),
                         static_cast<int>(isoMesh_.vertices.size() * sizeof(float)));
        shader_->enableAttributeArray(0);
        shader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
        shader_->enableAttributeArray(1);
        shader_->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
        isoIbo_->bind();
        isoIbo_->allocate(isoMesh_.indices.data(),
                          static_cast<int>(isoMesh_.indices.size() * sizeof(unsigned int)));
        isoVbo_.release();
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

void GLWidget::renderSelectionHighlight() {
    if (selEdgeVertCount_ <= 0 || !selection_.hasSelection()) return;

    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uWireAlpha", 1.0f);
    shader_->setUniformValue("uUseVertexColor", false);
    shader_->setUniformValue("uColor", QVector3D(1.0f, 0.78f, 0.0f));
    glDisable(GL_DEPTH_TEST);

    selEdgeVao_.bind();
    if (selHlMode_ == 1) {
        glPointSize(8.0f);
        glDrawArrays(GL_POINTS, 0, selEdgeVertCount_);
    } else {
        glLineWidth(2.5f);
        glDrawArrays(GL_LINES, 0, selEdgeVertCount_);
        glLineWidth(1.0f);
    }
    selEdgeVao_.release();
    glEnable(GL_DEPTH_TEST);
}

void GLWidget::render2DOverlays(const glm::mat4& mvp) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawAxesLabels(painter);
    if (showLabels_ && selection_.hasSelection())
        drawIdLabels(painter, mvp);
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

    // 重建拾取 FBO（尺寸需与视口一致）
    // 注意：resizeGL 由 Qt 调用时 GL 上下文已 current，无需手动 makeCurrent/doneCurrent
    int dpr = devicePixelRatio();
    delete pickFbo_;
    pickFbo_ = new QOpenGLFramebufferObject(w * dpr, h * dpr,
        QOpenGLFramebufferObject::Depth);

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
        silhouetteDirty_ = true;

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
            // 框选（ctrl/shift 持有 → 追加到现有选择集）
            pickRectPending_ = true;
            pendingPickRect_ = rect;
            pendingPickCtrl_ = ctrl;
            update();
        } else {
            // 范围太小视为点选
            pickPointPending_ = true;
            pendingPickPos_ = e->pos();
            pendingPickCtrl_ = ctrl;
            update();
        }
    }

    // ── Ctrl/Shift + 右键：取消选中（点选/框选） ──
    if (e->button() == Qt::RightButton && isBoxDeselecting_ && rubberBand_) {
        rubberBand_->hide();
        isBoxDeselecting_ = false;

        QRect rect = QRect(boxOrigin_, e->pos()).normalized();
        if (rect.width() > 3 && rect.height() > 3) {
            // 框选取消
            deselectRectPending_ = true;
            pendingDeselectRect_ = rect;
            update();
        } else {
            // 范围太小视为点选取消
            deselectPointPending_ = true;
            pendingDeselectPos_ = e->pos();
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
        silhouetteDirty_ = true;
    update();
}

void GLWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        if (selection_.hasSelection()) {
            selection_.clear();
            selectionDirty_ = true;
            partEdgeCacheValid_ = false;
            selEdgeVertCount_ = 0;
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
// 拾取功能
// ============================================================

glm::vec3 GLWidget::idToColor(int id) {
    // 将单元 ID+1 编码为 RGB 颜色（0 表示无命中）
    id += 1;
    int r = (id      ) & 0xFF;
    int g = (id >>  8) & 0xFF;
    int b = (id >> 16) & 0xFF;
    return glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

int GLWidget::colorToId(unsigned char r, unsigned char g, unsigned char b) {
    if (r == 0 && g == 0 && b == 0) return -1;  // 背景
    int id = r | (g << 8) | (b << 16);
    return id - 1;
}

void GLWidget::renderPickBuffer(const glm::mat4& mvp) {
    if (!pickFbo_ || triToElem_.empty()) return;

    GLint prevViewport[4];
    GLfloat prevClearColor[4];
    GLboolean prevDepthTest, prevBlend;
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
    glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
    glGetBooleanv(GL_BLEND, &prevBlend);

    // FBO 用原始 glBindFramebuffer 绑定，不走 QOpenGLFramebufferObject 的成员方法：
    // Qt 内部用 QOpenGLContextPrivate::current_fbo 追踪当前 FBO，调用 Qt 的绑定
    // 包装器会把追踪指向 pickFbo_，但恢复时必须用原始 GL 调用
    // (defaultFramebufferObject() 不能通过 Qt 包装器恢复)，导致 Qt 追踪停留在
    // 已失效的指针。后续 QPainter 构造会按这个指针写字体缓存到错误的 FBO，
    // 表现为 Windows GL2 文字引擎拾取后 drawText 静默失败（标签和坐标轴消失），
    // 直到下一次 makeCurrent/doneCurrent (例如切换主题) 重置追踪才恢复。
    // macOS 走不同的绘制引擎路径，对此不敏感。
    glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_->handle());

    int dpr = devicePixelRatio();
    glViewport(0, 0, width() * dpr, height() * dpr);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // 使用 Qt 包装器绑定着色器和 VAO，确保 Qt 状态追踪与实际 GL 状态一致
    // （之前使用原始 GL 调用会导致 QPainter 内部状态缓存失效，
    //  表现为拾取后 drawText 静默失败：标签和坐标轴文字消失）
    pickShader_->bind();
    pickShader_->setUniformValue("uMVP",
        QMatrix4x4(glm::value_ptr(glm::transpose(mvp))));

    GLint pickColorLoc = pickShader_->uniformLocation("uPickColor");

    pickVao_.bind();
    vbo_.bind();
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    GLuint rawIbo = 0;
    glGenBuffers(1, &rawIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rawIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(allTriIndices_.size() * sizeof(unsigned int)),
                 allTriIndices_.data(), GL_STATIC_DRAW);

    int triCount = static_cast<int>(triToElem_.size());
    int i = 0;
    while (i < triCount) {
        int elemId = triToElem_[i];
        int start = i;
        while (i < triCount && triToElem_[i] == elemId) ++i;

        if (start < static_cast<int>(triToPart_.size())) {
            int partIdx = triToPart_[start];
            if (partIdx >= 0) {
                auto it = partVisibility_.find(partIdx);
                if (it != partVisibility_.end() && !it->second)
                    continue;
            }
        }

        glm::vec3 c = idToColor(elemId);
        glUniform3f(pickColorLoc, c.x, c.y, c.z);
        glDrawElements(GL_TRIANGLES, (i - start) * 3, GL_UNSIGNED_INT,
                       reinterpret_cast<void*>(start * 3 * sizeof(unsigned int)));
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &rawIbo);

    pickVao_.release();
    pickShader_->release();
    // 释放 ARRAY_BUFFER 绑定。VAO release 不会自动解绑 ARRAY_BUFFER（global state），
    // 残留绑定在 Windows Qt 5 GL2 文字引擎里会让后续 QPainter::drawText 静默失败。
    vbo_.release();

    // 恢复 GL 状态
    bindWidgetFramebuffer();
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

void GLWidget::pickAtPoint(const QPoint& pos, bool ctrlHeld) {
    if (!pickFbo_ || triToElem_.empty()) return;

    // 注意：此函数仅在 paintGL() 内调用，GL 上下文已由 Qt 管理，
    // 无需手动 makeCurrent/doneCurrent。

    // 渲染拾取缓冲
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, cam_.distance * 0.01f, cam_.distance * 10.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;

    renderPickBuffer(mvp);

    unsigned char pixel[4] = {0};
    {
        // 原始 GL 绑定，避免污染 Qt 的 current_fbo 追踪（详见 renderPickBuffer 注释）
        glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_->handle());
        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;
        glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        bindWidgetFramebuffer();
    }

    int elemId = colorToId(pixel[0], pixel[1], pixel[2]);

    if (pickMode_ == PickMode::Node) {
        // ── 节点拾取：找到点击处最近的顶点 ──
        int closestNode = -1;
        if (elemId >= 0) {
            float ndcX = (2.0f * pos.x() / width()) - 1.0f;
            float ndcY = 1.0f - (2.0f * pos.y() / height());
            float minDist2 = 1e30f;
            int triCount = static_cast<int>(triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (triToElem_[t] != elemId) continue;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = mesh_.indices[t * 3 + v];
                    glm::vec4 wp(mesh_.vertices[vi * 6],
                                 mesh_.vertices[vi * 6 + 1],
                                 mesh_.vertices[vi * 6 + 2], 1.0f);
                    glm::vec4 clip = mvp * wp;
                    if (clip.w <= 0) continue;
                    float sx = clip.x / clip.w;
                    float sy = clip.y / clip.w;
                    float d2 = (sx - ndcX) * (sx - ndcX) + (sy - ndcY) * (sy - ndcY);
                    if (d2 < minDist2) {
                        minDist2 = d2;
                        closestNode = (vi < vertexToNode_.size()) ? vertexToNode_[vi] : static_cast<int>(vi);
                    }
                }
            }
        }
        if (!ctrlHeld) {
            selection_.clear();
            if (closestNode >= 0) selection_.selectedNodes.insert(closestNode);
        } else {
            if (closestNode >= 0) selection_.toggleNode(closestNode);
        }

    } else if (pickMode_ == PickMode::Part) {
        // ── 部件拾取：命中单元 → elemToPart_ O(1) 查找部件索引 ──
        int hitPart = -1;
        if (elemId >= 0 && !elemToPart_.empty()) {
            auto it = elemToPart_.find(elemId);
            if (it != elemToPart_.end()) hitPart = it->second;
        }
        if (!ctrlHeld) {
            selection_.clear();
            if (hitPart >= 0) selectPart(hitPart);
        } else {
            if (hitPart >= 0) {
                if (isPartFullySelected(hitPart))
                    deselectPart(hitPart);
                else
                    selectPart(hitPart);
            }
        }

    } else {
        // ── 单元拾取 ──
        if (!ctrlHeld) {
            selection_.clear();
            if (elemId >= 0) selection_.selectedElements.insert(elemId);
        } else {
            if (elemId >= 0) selection_.toggleElement(elemId);
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        PickMode emitMode = pickMode_;
        if (!selection_.selectedNodes.empty()) {
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        } else {
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        }
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(emitMode, static_cast<int>(ids.size()), ids);
    }
    // 部件模式：发射选中的部件索引列表，同步模型树
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
            if (isPartFullySelected(pi))
                pickedParts.push_back(pi);
        }
        emit partsPicked(pickedParts);
    }
}

void GLWidget::pickInRect(const QRect& rect, bool ctrlHeld) {
    if (triToElem_.empty()) return;

    // 注意：此函数现在仅在 paintGL() 内调用，GL 上下文已由 Qt 管理。

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, cam_.distance * 0.01f, cam_.distance * 10.0f);
    glm::mat4 mvp = projection * cam_.viewMatrix();

    // 框选范围转换为 NDC 坐标
    float ndcL = (2.0f * rect.left() / width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    // ctrlHeld 时累加到现有选择集（追加框选）；否则替换
    if (!ctrlHeld) selection_.clear();

    int vertCount = static_cast<int>(mesh_.vertices.size() / 6);

    if (pickMode_ == PickMode::Node) {
        // 节点模式：遍历所有渲染顶点，投影到屏幕判断是否在框内
        std::unordered_set<int> addedNodes;
        for (int vi = 0; vi < vertCount; ++vi) {
            glm::vec4 wp(mesh_.vertices[vi * 6],
                         mesh_.vertices[vi * 6 + 1],
                         mesh_.vertices[vi * 6 + 2], 1.0f);
            glm::vec4 clip = mvp * wp;
            if (clip.w <= 0) continue;
            float sx = clip.x / clip.w;
            float sy = clip.y / clip.w;
            if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                int nodeId = (vi < static_cast<int>(vertexToNode_.size())) ? vertexToNode_[vi] : vi;
                if (nodeId >= 0 && addedNodes.insert(nodeId).second)
                    selection_.selectedNodes.insert(nodeId);
            }
        }
    } else if (pickMode_ == PickMode::Part) {
        // 部件模式：框内三角形 → 收集部件索引 → 选中这些部件所有单元
        std::unordered_set<int> hitParts;
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6],
                             mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside && t < static_cast<int>(triToPart_.size())) {
                hitParts.insert(triToPart_[t]);
            }
        }
        for (int p : hitParts) {
            selectPart(p);
        }
    } else {
        // 单元模式：遍历所有三角形，如果三角形任意一个顶点在框内则选中该单元
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            int elemId = triToElem_[t];
            if (selection_.isElementSelected(elemId)) continue;  // 已选中，跳过

            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6],
                             mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside) {
                selection_.selectedElements.insert(elemId);
            }
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        PickMode emitMode = pickMode_;
        if (!selection_.selectedNodes.empty()) {
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        } else {
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        }
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(emitMode, static_cast<int>(ids.size()), ids);
    }
    // 部件模式：发射选中的部件索引列表，同步模型树
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
            if (isPartFullySelected(pi))
                pickedParts.push_back(pi);
        }
        emit partsPicked(pickedParts);
    }
}

void GLWidget::deselectAtPoint(const QPoint& pos) {
    if (!pickFbo_ || triToElem_.empty()) return;

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, cam_.distance * 0.01f, cam_.distance * 10.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;

    renderPickBuffer(mvp);

    unsigned char pixel[4] = {0};
    {
        // 原始 GL 绑定，避免污染 Qt 的 current_fbo 追踪（详见 renderPickBuffer 注释）
        glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_->handle());
        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;
        glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        bindWidgetFramebuffer();
    }

    int elemId = colorToId(pixel[0], pixel[1], pixel[2]);

    if (pickMode_ == PickMode::Node) {
        int closestNode = -1;
        if (elemId >= 0) {
            float ndcX = (2.0f * pos.x() / width()) - 1.0f;
            float ndcY = 1.0f - (2.0f * pos.y() / height());
            float minDist2 = 1e30f;
            int triCount = static_cast<int>(triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (triToElem_[t] != elemId) continue;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = mesh_.indices[t * 3 + v];
                    glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                                 mesh_.vertices[vi * 6 + 2], 1.0f);
                    glm::vec4 clip = mvp * wp;
                    if (clip.w <= 0) continue;
                    float sx = clip.x / clip.w;
                    float sy = clip.y / clip.w;
                    float d2 = (sx - ndcX) * (sx - ndcX) + (sy - ndcY) * (sy - ndcY);
                    if (d2 < minDist2) { minDist2 = d2; closestNode = (vi < vertexToNode_.size()) ? vertexToNode_[vi] : static_cast<int>(vi); }
                }
            }
        }
        if (closestNode >= 0) selection_.selectedNodes.erase(closestNode);

    } else if (pickMode_ == PickMode::Part) {
        if (elemId >= 0 && !elemToPart_.empty()) {
            auto it = elemToPart_.find(elemId);
            if (it != elemToPart_.end()) deselectPart(it->second);
        }

    } else {
        if (elemId >= 0) selection_.selectedElements.erase(elemId);
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        if (!selection_.selectedNodes.empty())
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    }
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }
}

void GLWidget::deselectInRect(const QRect& rect) {
    if (triToElem_.empty()) return;

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, cam_.distance * 0.01f, cam_.distance * 10.0f);
    glm::mat4 mvp = projection * cam_.viewMatrix();

    float ndcL = (2.0f * rect.left() / width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    int vertCount = static_cast<int>(mesh_.vertices.size() / 6);

    if (pickMode_ == PickMode::Node) {
        std::unordered_set<int> removedNodes;
        for (int vi = 0; vi < vertCount; ++vi) {
            glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                         mesh_.vertices[vi * 6 + 2], 1.0f);
            glm::vec4 clip = mvp * wp;
            if (clip.w <= 0) continue;
            float sx = clip.x / clip.w;
            float sy = clip.y / clip.w;
            if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                int nodeId = (vi < static_cast<int>(vertexToNode_.size())) ? vertexToNode_[vi] : vi;
                if (nodeId >= 0 && removedNodes.insert(nodeId).second)
                    selection_.selectedNodes.erase(nodeId);
            }
        }
    } else if (pickMode_ == PickMode::Part) {
        std::unordered_set<int> hitParts;
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) { anyInside = true; break; }
            }
            if (anyInside && t < static_cast<int>(triToPart_.size()))
                hitParts.insert(triToPart_[t]);
        }
        for (int p : hitParts) deselectPart(p);
    } else {
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            int elemId = triToElem_[t];
            if (!selection_.isElementSelected(elemId)) continue;
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) { anyInside = true; break; }
            }
            if (anyInside) selection_.selectedElements.erase(elemId);
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        if (!selection_.selectedNodes.empty())
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    }
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
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
        selectionDirty_ = true;
        partEdgeCacheValid_ = false;
        selEdgeVertCount_ = 0;
        emit selectionChanged(pickMode_, 0, {});
        if (mode == PickMode::Part)
            emit partsPicked({});
        update();
    }
}

void GLWidget::selectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return;
    for (int eid : partElementIds_[partIndex]) {
        selection_.selectedElements.insert(eid);
    }
}

void GLWidget::deselectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return;
    for (int eid : partElementIds_[partIndex]) {
        selection_.selectedElements.erase(eid);
    }
}

bool GLWidget::isPartFullySelected(int partIndex) const {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return false;
    const auto& elems = partElementIds_[partIndex];
    if (elems.empty()) return false;
    for (int eid : elems) {
        if (!selection_.isElementSelected(eid)) return false;
    }
    return true;
}

void GLWidget::rebuildSelectionEdges() {
    int edgeCount = static_cast<int>(mesh_.elemEdgeToElement.size());
    std::vector<float> verts;

    if (pickMode_ == PickMode::Part && !vertexToNode_.empty()) {
        // 部件模式：使用缓存机制，避免每帧重建 edgeMap
        if (!partEdgeCacheValid_) {
            buildPartEdgeCache();
        }
        updateSilhouetteFromCache();
        return;  // VBO 已在 updateSilhouetteFromCache 中上传
    } else {
        // 单元模式：显示所有选中单元的全部边线
        for (int i = 0; i < edgeCount; ++i) {
            if (!selection_.isElementSelected(mesh_.elemEdgeToElement[i])) continue;

            int base = i * 6;
            for (int j = 0; j < 6; ++j)
                verts.push_back(mesh_.elemEdgeVertices[base + j]);
        }
    }

    selEdgeVertCount_ = static_cast<int>(verts.size() / 3);

    selEdgeVao_.bind();
    selEdgeVbo_.bind();
    if (!verts.empty()) {
        selEdgeVbo_.allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));
    }
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    selEdgeVao_.release();
    // 释放 ARRAY_BUFFER 全局绑定。VAO release 不会自动解绑 ARRAY_BUFFER（它是 global state，
    // 不是 VAO state），残留的 bind 在 Windows Qt 5 GL2 文字引擎中会让后续 QPainter::drawText
    // 提交到错误的 buffer，表现为拾取后标签静默消失，直到 makeCurrent/doneCurrent 循环
    // (如切换主题) 重置 paint engine 才恢复。macOS 走不同的绘制引擎路径，对此不敏感。
    selEdgeVbo_.release();
}

void GLWidget::buildEdgeAdjacency() {
    edgeAdjMap_.clear();
    edgeAdjDirty_ = false;

    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    if (triCount == 0) return;

    // 预分配（每个三角形 3 条边，约 50% 共享 → ~1.5x triCount 条边）
    edgeAdjMap_.reserve(triCount * 2);

    for (int t = 0; t < triCount; ++t) {
        for (int e = 0; e < 3; ++e) {
            unsigned int vi_a = mesh_.indices[t * 3 + e];
            unsigned int vi_b = mesh_.indices[t * 3 + (e + 1) % 3];
            int na = (vi_a < vertexToNode_.size()) ? vertexToNode_[vi_a] : static_cast<int>(vi_a);
            int nb = (vi_b < vertexToNode_.size()) ? vertexToNode_[vi_b] : static_cast<int>(vi_b);
            int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) | static_cast<uint32_t>(std::max(na, nb));

            auto& pe = edgeAdjMap_[key];
            if (pe.adjTris.empty()) { pe.va = vi_a; pe.vb = vi_b; }
            pe.adjTris.push_back(t);
        }
    }
}

void GLWidget::buildPartEdgeCache() {
    // 确保边邻接表已构建
    if (edgeAdjDirty_) buildEdgeAdjacency();

    cachedStaticEdgeVerts_.clear();
    cachedSilhouettes_.clear();

    // ── 1. 收集选中且可见的部件索引 ──
    std::unordered_set<int> selectedParts;
    int numParts = static_cast<int>(partElementIds_.size());
    for (int p = 0; p < numParts; ++p) {
        auto vit = partVisibility_.find(p);
        if (vit != partVisibility_.end() && !vit->second) continue;
        for (int eid : partElementIds_[p]) {
            if (selection_.isElementSelected(eid)) {
                selectedParts.insert(p);
                break;
            }
        }
    }

    if (selectedParts.empty()) {
        partEdgeCacheValid_ = true;
        return;
    }

    // ── 2. 只遍历选中部件的三角形，收集边并分类 ──
    const float featureAngleThreshold = 0.5f;  // cos(60°)

    auto triNormal = [&](int t) -> glm::vec3 {
        unsigned int i0 = mesh_.indices[t * 3];
        unsigned int i1 = mesh_.indices[t * 3 + 1];
        unsigned int i2 = mesh_.indices[t * 3 + 2];
        glm::vec3 p0(mesh_.vertices[i0 * 6], mesh_.vertices[i0 * 6 + 1], mesh_.vertices[i0 * 6 + 2]);
        glm::vec3 p1(mesh_.vertices[i1 * 6], mesh_.vertices[i1 * 6 + 1], mesh_.vertices[i1 * 6 + 2]);
        glm::vec3 p2(mesh_.vertices[i2 * 6], mesh_.vertices[i2 * 6 + 1], mesh_.vertices[i2 * 6 + 2]);
        glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
        float len = glm::length(cr);
        return (len > 1e-12f) ? cr / len : glm::vec3(0.0f);
    };

    auto pushEdgeVerts = [&](unsigned int a, unsigned int b, std::vector<float>& out) {
        out.push_back(mesh_.vertices[a * 6]);
        out.push_back(mesh_.vertices[a * 6 + 1]);
        out.push_back(mesh_.vertices[a * 6 + 2]);
        out.push_back(mesh_.vertices[b * 6]);
        out.push_back(mesh_.vertices[b * 6 + 1]);
        out.push_back(mesh_.vertices[b * 6 + 2]);
    };

    // 用 visited 集合确保每条边只处理一次
    std::unordered_set<int64_t> visitedEdges;

    // 预估容量（减少 rehash）
    int totalSelectedTris = 0;
    for (int p : selectedParts) totalSelectedTris += static_cast<int>(partTriangles_[p].size());
    visitedEdges.reserve(totalSelectedTris * 2);
    cachedStaticEdgeVerts_.reserve(totalSelectedTris * 6);

    for (int p : selectedParts) {
        for (int t : partTriangles_[p]) {
            for (int e = 0; e < 3; ++e) {
                unsigned int vi_a = mesh_.indices[t * 3 + e];
                unsigned int vi_b = mesh_.indices[t * 3 + (e + 1) % 3];
                int na = (vi_a < vertexToNode_.size()) ? vertexToNode_[vi_a] : static_cast<int>(vi_a);
                int nb = (vi_b < vertexToNode_.size()) ? vertexToNode_[vi_b] : static_cast<int>(vi_b);
                int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) | static_cast<uint32_t>(std::max(na, nb));

                if (!visitedEdges.insert(key).second) continue;  // 已处理

                auto it = edgeAdjMap_.find(key);
                if (it == edgeAdjMap_.end()) continue;

                const PreEdge& pe = it->second;

                // 分类邻接三角形
                int selectedTriCount = 0;
                int otherTriCount = 0;
                int selTri0 = -1, selTri1 = -1;

                for (int adjT : pe.adjTris) {
                    int adjPart = (adjT < static_cast<int>(triToPart_.size())) ? triToPart_[adjT] : -1;
                    if (adjPart >= 0 && selectedParts.count(adjPart)) {
                        if (selectedTriCount == 0) selTri0 = adjT;
                        else if (selectedTriCount == 1) selTri1 = adjT;
                        selectedTriCount++;
                    } else {
                        otherTriCount++;
                    }
                }

                // 边界边（与非选中部件共享）
                if (otherTriCount > 0) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 开放边（只有一个三角形）
                if (selectedTriCount == 1) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 特征边 or 轮廓边候选
                if (selectedTriCount >= 2 && selTri0 >= 0 && selTri1 >= 0) {
                    glm::vec3 n0 = triNormal(selTri0);
                    glm::vec3 n1 = triNormal(selTri1);
                    if (glm::dot(n0, n1) < featureAngleThreshold) {
                        pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    } else {
                        SilhouetteCandidate sc;
                        sc.ax = mesh_.vertices[pe.va * 6];
                        sc.ay = mesh_.vertices[pe.va * 6 + 1];
                        sc.az = mesh_.vertices[pe.va * 6 + 2];
                        sc.bx = mesh_.vertices[pe.vb * 6];
                        sc.by = mesh_.vertices[pe.vb * 6 + 1];
                        sc.bz = mesh_.vertices[pe.vb * 6 + 2];
                        sc.n0 = n0;
                        sc.n1 = n1;
                        cachedSilhouettes_.push_back(sc);
                    }
                }
            }
        }
    }

    partEdgeCacheValid_ = true;
}

void GLWidget::updateSilhouetteFromCache() {
    // 预分配：静态边 + 最大可能的轮廓边
    size_t staticSize = cachedStaticEdgeVerts_.size();
    std::vector<float> verts;
    verts.reserve(staticSize + cachedSilhouettes_.size() * 6);

    // 复制静态边（边界/特征/开放）
    verts.insert(verts.end(), cachedStaticEdgeVerts_.begin(), cachedStaticEdgeVerts_.end());

    // 添加视角依赖的轮廓边
    glm::vec3 eyePos = cam_.eye();
    for (const auto& sc : cachedSilhouettes_) {
        glm::vec3 edgeMid((sc.ax + sc.bx) * 0.5f,
                          (sc.ay + sc.by) * 0.5f,
                          (sc.az + sc.bz) * 0.5f);
        glm::vec3 viewDir = eyePos - edgeMid;
        float d0 = glm::dot(sc.n0, viewDir);
        float d1 = glm::dot(sc.n1, viewDir);
        if (d0 * d1 <= 0.0f) {
            verts.push_back(sc.ax); verts.push_back(sc.ay); verts.push_back(sc.az);
            verts.push_back(sc.bx); verts.push_back(sc.by); verts.push_back(sc.bz);
        }
    }

    selEdgeVertCount_ = static_cast<int>(verts.size() / 3);

    selEdgeVao_.bind();
    selEdgeVbo_.bind();
    if (!verts.empty())
        selEdgeVbo_.allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    selEdgeVao_.release();
    selEdgeVbo_.release();
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

    // 保存投影参数，供 drawAxesLabels() 使用
    axesMVP_ = axesMVP;
}

void GLWidget::drawAxesLabels(QPainter& painter) {
    const int axesSize = 120;
    const int margin = 8;

    auto project = [&](glm::vec3 pt) -> QPointF {
        glm::vec4 clip = axesMVP_ * glm::vec4(pt, 1.0f);
        float sx = margin + (clip.x / clip.w * 0.5f + 0.5f) * axesSize;
        float sy = height() - margin - (clip.y / clip.w * 0.5f + 0.5f) * axesSize;
        return QPointF(sx, sy);
    };

    struct AxisLabel { glm::vec3 dir; QString name; QColor color; };
    AxisLabel labels[] = {
        {{1,0,0}, "X", QColor(240, 80, 80)},
        {{0,1,0}, "Y", QColor(90, 220, 90)},
        {{0,0,1}, "Z", QColor(90, 140, 255)},
    };

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(14);
    painter.setFont(font);

    for (const auto& l : labels) {
        QPointF pos = project(l.dir * 1.15f);
        painter.setPen(l.color);
        painter.drawText(QRectF(pos.x() - 12, pos.y() - 12, 24, 24),
                         Qt::AlignCenter, l.name);
    }
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
            if (validNodes.count(id))
                selection_.selectedNodes.insert(id);
        }
    } else if (mode == PickMode::Part) {
        for (int pi : ids) selectPart(pi);
    } else {
        // 过滤：只保留渲染网格中存在的单元 ID（含三角面和 1D 边线）
        std::unordered_set<int> validElems(triToElem_.begin(), triToElem_.end());
        for (int eid : mesh_.elemEdgeToElement)
            validElems.insert(eid);
        for (int id : ids) {
            if (validElems.count(id))
                selection_.selectedElements.insert(id);
        }
    }

    selectionDirty_ = true;

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
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }

    update();
}

void GLWidget::drawIdLabels(QPainter& painter, const glm::mat4& mvp) {
    int w = width();
    int h = height();

    // 世界坐标 → 屏幕坐标
    auto project = [&](const glm::vec3& pos) -> QPointF {
        glm::vec4 clip = mvp * glm::vec4(pos, 1.0f);
        if (clip.w <= 0.0f) return QPointF(-1, -1);
        float nx = clip.x / clip.w;
        float ny = clip.y / clip.w;
        float sx = (nx * 0.5f + 0.5f) * w;
        float sy = (1.0f - (ny * 0.5f + 0.5f)) * h;
        return QPointF(sx, sy);
    };

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);

    // 描边文字：深色轮廓 + 亮色正文（避免 drawRect 导致 GL 状态崩溃）
    QColor outlineColor(0, 0, 0, 220);
    QColor textColor(255, 200, 0);
    const int offsetY = -14;  // 标签偏移到实体上方

    // 绘制带描边的文字（4方向偏移描边 + 正文叠加）
    auto drawOutlinedText = [&](int x, int y, const QString& text) {
        painter.setPen(outlineColor);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    painter.drawText(x + dx, y + dy, text);
        painter.setPen(textColor);
        painter.drawText(x, y, text);
    };

    QFontMetrics fm(font);

    if (pickMode_ == PickMode::Node) {
        // ── 节点标签 ──
        if (!selection_.selectedNodes.empty() && !vertexToNode_.empty()) {
            std::unordered_map<int, int> nodeToVert;
            for (int i = 0; i < static_cast<int>(vertexToNode_.size()); ++i) {
                int nid = vertexToNode_[i];
                if (nid >= 0 && nodeToVert.find(nid) == nodeToVert.end())
                    nodeToVert[nid] = i;
            }

            for (int nid : selection_.selectedNodes) {
                auto it = nodeToVert.find(nid);
                if (it == nodeToVert.end()) continue;
                int vi = it->second;
                if (vi * 6 + 2 >= static_cast<int>(mesh_.vertices.size())) continue;

                glm::vec3 pos(mesh_.vertices[vi * 6],
                              mesh_.vertices[vi * 6 + 1],
                              mesh_.vertices[vi * 6 + 2]);
                QPointF sp = project(pos);
                if (sp.x() < 0) continue;

                QString text = QString::number(nid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }

    } else if (pickMode_ == PickMode::Part) {
        // ── 部件标签（在部件重心位置显示部件索引） ──
        if (!selection_.selectedElements.empty() && !triToElem_.empty() && !triToPart_.empty()) {
            // 收集选中的部件索引
            std::set<int> selectedParts;
            for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
                if (isPartFullySelected(pi))
                    selectedParts.insert(pi);
            }

            // 计算每个选中部件的重心
            for (int pi : selectedParts) {
                if (pi < 0 || pi >= static_cast<int>(partTriangles_.size())) continue;
                float sx = 0, sy = 0, sz = 0;
                int count = 0;
                for (int t : partTriangles_[pi]) {
                    if (t * 3 + 2 >= static_cast<int>(mesh_.indices.size())) continue;
                    for (int k = 0; k < 3; ++k) {
                        unsigned int vi = mesh_.indices[t * 3 + k];
                        if (vi * 6 + 2 < mesh_.vertices.size()) {
                            sx += mesh_.vertices[vi * 6];
                            sy += mesh_.vertices[vi * 6 + 1];
                            sz += mesh_.vertices[vi * 6 + 2];
                            count++;
                        }
                    }
                }
                if (count == 0) continue;
                glm::vec3 center(sx / count, sy / count, sz / count);
                QPointF sp = project(center);
                if (sp.x() < 0) continue;

                QString text = QString("Part %1").arg(pi + 1);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }

    } else {
        // ── 单元标签（在单元重心位置显示） ──
        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            struct ElemAccum { float sx = 0, sy = 0, sz = 0; int count = 0; };
            std::unordered_map<int, ElemAccum> elemCentroids;

            int triCount = static_cast<int>(triToElem_.size());
            int idxCount = static_cast<int>(mesh_.indices.size());
            for (int t = 0; t < triCount; ++t) {
                if (t * 3 + 2 >= idxCount) break;
                int eid = triToElem_[t];
                if (selection_.selectedElements.count(eid) == 0) continue;
                auto& acc = elemCentroids[eid];
                for (int k = 0; k < 3; ++k) {
                    unsigned int vi = mesh_.indices[t * 3 + k];
                    if (vi * 6 + 2 < mesh_.vertices.size()) {
                        acc.sx += mesh_.vertices[vi * 6];
                        acc.sy += mesh_.vertices[vi * 6 + 1];
                        acc.sz += mesh_.vertices[vi * 6 + 2];
                        acc.count++;
                    }
                }
            }

            for (const auto& [eid, acc] : elemCentroids) {
                if (acc.count == 0) continue;
                glm::vec3 center(acc.sx / acc.count, acc.sy / acc.count, acc.sz / acc.count);
                QPointF sp = project(center);
                if (sp.x() < 0) continue;

                QString text = QString::number(eid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }
    }
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
        bgVbo_.bind();
        bgVbo_.write(0, bgData, sizeof(bgData));
        bgVbo_.release();
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

    // 上传 triToPart 到 texture buffer（供片段着色器用 gl_PrimitiveID 查表）
    triPartDirty_ = true;
    needsColorUpload_ = true;
    update();
}

void GLWidget::setEdgeToPartMap(const std::vector<int>& map) {
    edgeToPart_ = map;
    edgeVisibilityDirty_ = true;
    update();
}

void GLWidget::setPartVisibility(int partIndex, bool visible) {
    partVisibility_[partIndex] = visible;
    partVisibilityDirty_ = true;
    edgeVisibilityDirty_ = true;
    // 可见性变化影响选中高亮（隐藏部件不显示高亮）
    if (selection_.hasSelection()) {
        partEdgeCacheValid_ = false;
        selectionDirty_ = true;
    }
    update();
}

void GLWidget::highlightParts(const std::vector<int>& partIndices) {
    // 清除当前选中
    selection_.selectedElements.clear();
    selection_.selectedNodes.clear();

    // 将指定部件的所有单元加入选中
    for (int pi : partIndices)
        selectPart(pi);

    // 触发高亮重建
    partEdgeCacheValid_ = false;
    selectionDirty_ = true;

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

    std::vector<unsigned int> filtered;
    filtered.reserve(allEdgeIndices_.size());
    int edgeCount = static_cast<int>(allEdgeIndices_.size() / 2);
    for (int e = 0; e < edgeCount; ++e) {
        int part = (e < static_cast<int>(edgeToPart_.size())) ? edgeToPart_[e] : -1;
        if (part >= 0) {
            auto it = partVisibility_.find(part);
            if (it != partVisibility_.end() && !it->second)
                continue;   // 该部件不可见，跳过此边
        }
        filtered.push_back(allEdgeIndices_[e * 2]);
        filtered.push_back(allEdgeIndices_[e * 2 + 1]);
    }
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
    vbo_.bind();
    vbo_.allocate(mesh_.vertices.data(),
                  static_cast<int>(mesh_.vertices.size() * sizeof(float)));

    // 上传索引数据到 IBO
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

    // ── 颜色缓冲（per-vertex，默认为 color_） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultColors(vertCount * 3);
        for (int v = 0; v < vertCount; ++v) {
            defaultColors[v * 3 + 0] = color_.x;
            defaultColors[v * 3 + 1] = color_.y;
            defaultColors[v * 3 + 2] = color_.z;
        }
        colorVbo_.bind();
        colorVbo_.allocate(defaultColors.data(), static_cast<int>(defaultColors.size() * sizeof(float)));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(2);
    }

    // ── 标量值缓冲（per-vertex，默认全 0） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultScalars(vertCount, 0.0f);
        scalarVbo_.bind();
        scalarVbo_.allocate(defaultScalars.data(), static_cast<int>(defaultScalars.size() * sizeof(float)));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
        glEnableVertexAttribArray(3);
        scalarVbo_.release();
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

        edgeVbo_.bind();
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

        edgeVbo_.release();
        edgeVao_.release();
    }
}
