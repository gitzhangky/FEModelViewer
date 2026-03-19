/**
 * @file GLWidget.cpp
 * @brief OpenGL 渲染窗口组件实现
 */

#include "GLWidget.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>

// ============================================================
// GLSL 着色器源码
// ============================================================

/**
 * 顶点着色器：
 *   - 将顶点位置变换到裁剪空间（uMVP）
 *   - 计算世界空间位置和法线，传递给片段着色器用于光照计算
 */
static const char* vertSrc = R"(
#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal   = normalize(uNormalMat * aNormal);
    vColor    = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

/**
 * 片段着色器（Blinn-Phong 光照模型）：
 *   - 线框模式：直接输出纯色
 *   - 光照模式：ambient + diffuse + specular 三分量叠加
 */
static const char* fragSrc = R"(
#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;

uniform vec3 uLightDir;
uniform vec3 uColor;
uniform bool uWireframe;
uniform bool uUseVertexColor;
out vec4 outColor;

void main() {
    vec3 surfaceColor = uUseVertexColor ? vColor : uColor;

    if (uWireframe) {
        outColor = vec4(surfaceColor, 1.0);
        return;
    }

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 L = normalize(-uLightDir);

    float ambient  = 0.45;
    float diffuse  = max(dot(N, L), 0.0) * 0.35;
    float sideFactor = gl_FrontFacing ? 1.0 : 0.85;
    vec3 color = surfaceColor * (ambient + diffuse) * sideFactor;
    outColor = vec4(color, 1.0);
}
)";

// ============================================================
// 坐标轴指示器着色器（纯色线段，顶点自带颜色）
// ============================================================

static const char* axesVertSrc = R"(
#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 uMVP;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* axesFragSrc = R"(
#version 410 core
in vec3 vColor;
out vec4 outColor;

void main() {
    outColor = vec4(vColor, 1.0);
}
)";

// ============================================================
// 拾取着色器（每个三角形用唯一颜色编码单元 ID）
// ============================================================

static const char* pickVertSrc = R"(
#version 410 core
layout (location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* pickFragSrc = R"(
#version 410 core
uniform vec3 uPickColor;
out vec4 outColor;
void main() {
    outColor = vec4(uPickColor, 1.0);
}
)";

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

// ============================================================
// 构造函数 & 公有方法
// ============================================================

GLWidget::GLWidget(QWidget* parent) : QOpenGLWidget(parent) {
    // 设置强焦点策略，使 widget 能接收键盘事件
    setFocusPolicy(Qt::StrongFocus);
}

void GLWidget::setMesh(const Mesh& mesh) {
    mesh_ = mesh;
    allTriIndices_ = mesh.indices;
    allEdgeIndices_ = mesh.edgeIndices;
    activeEdgeIndexCount_ = static_cast<int>(mesh.edgeIndices.size());
    triToPart_.clear();
    edgeToPart_.clear();
    partVisibility_.clear();
    partColors_.clear();
    activeIndexCount_ = static_cast<int>(mesh.indices.size());
    partVisibilityDirty_ = false;
    edgeVisibilityDirty_ = false;
    needsColorUpload_ = false;
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
    shader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc);
    shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc);
    shader_->link();

    // 创建 VAO（顶点数组对象）、VBO（顶点缓冲）、IBO（索引缓冲）
    vao_.create();
    vbo_.create();
    ibo_ = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    ibo_->create();
    colorVbo_.create();

    // ── 拾取着色器 ──
    pickShader_ = new QOpenGLShaderProgram(this);
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, pickVertSrc);
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, pickFragSrc);
    pickShader_->link();

    // ── 边线 VAO/VBO（FE 模式专用）──
    edgeVao_.create();
    edgeVbo_.create();

    // ── 选中高亮边线 VAO/VBO ──
    selEdgeVao_.create();
    selEdgeVbo_.create();

    // ── 坐标轴指示器着色器 ──
    axesShader_ = new QOpenGLShaderProgram(this);
    axesShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, axesVertSrc);
    axesShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, axesFragSrc);
    axesShader_->link();

    // ── 生成坐标轴几何数据（轴杆线段 + 箭头圆锥 + 中心球）──
    // 每顶点 6 float: pos(3) + color(3)
    std::vector<float> lineVerts;   // GL_LINES
    std::vector<float> triVerts;    // GL_TRIANGLES

    const int segs = 12;  // 圆锥/球体分段数
    const float shaftLen = 0.75f;   // 轴杆长度
    const float coneBase = 0.08f;   // 圆锥底面半径
    const float coneLen = 0.25f;    // 圆锥长度
    const float ballR = 0.06f;      // 中心球半径

    struct Axis { glm::vec3 dir; glm::vec3 up; glm::vec3 color; };
    Axis axes[] = {
        {{1,0,0}, {0,1,0}, {1.0f, 0.25f, 0.25f}},  // X 红
        {{0,1,0}, {0,0,1}, {0.3f, 0.85f, 0.3f}},    // Y 绿
        {{0,0,1}, {1,0,0}, {0.3f, 0.5f, 1.0f}},     // Z 蓝
    };

    for (auto& a : axes) {
        glm::vec3 right = glm::normalize(glm::cross(a.dir, a.up));
        glm::vec3 up2 = glm::normalize(glm::cross(right, a.dir));

        // 轴杆线段
        auto pushLine = [&](glm::vec3 p) {
            lineVerts.push_back(p.x); lineVerts.push_back(p.y); lineVerts.push_back(p.z);
            lineVerts.push_back(a.color.x); lineVerts.push_back(a.color.y); lineVerts.push_back(a.color.z);
        };
        pushLine(glm::vec3(0));
        pushLine(a.dir * shaftLen);

        // 箭头圆锥
        glm::vec3 tip = a.dir * (shaftLen + coneLen);
        auto pushTri = [&](glm::vec3 p, glm::vec3 c) {
            triVerts.push_back(p.x); triVerts.push_back(p.y); triVerts.push_back(p.z);
            triVerts.push_back(c.x); triVerts.push_back(c.y); triVerts.push_back(c.z);
        };
        for (int i = 0; i < segs; ++i) {
            float a0 = 2.0f * 3.14159265f * i / segs;
            float a1 = 2.0f * 3.14159265f * (i + 1) / segs;
            glm::vec3 b0 = a.dir * shaftLen + (right * cosf(a0) + up2 * sinf(a0)) * coneBase;
            glm::vec3 b1 = a.dir * shaftLen + (right * cosf(a1) + up2 * sinf(a1)) * coneBase;
            // 侧面三角
            pushTri(tip, a.color);
            pushTri(b0, a.color * 0.7f);
            pushTri(b1, a.color * 0.7f);
            // 底面三角
            pushTri(a.dir * shaftLen, a.color * 0.5f);
            pushTri(b1, a.color * 0.5f);
            pushTri(b0, a.color * 0.5f);
        }
    }

    // 中心球（八面体近似，16 面）
    glm::vec3 ballColor(0.75f, 0.75f, 0.78f);
    glm::vec3 bv[] = {
        { ballR,0,0}, {-ballR,0,0},
        {0, ballR,0}, {0,-ballR,0},
        {0,0, ballR}, {0,0,-ballR},
    };
    // 8 面
    int octFaces[][3] = {
        {0,2,4}, {0,4,3}, {0,3,5}, {0,5,2},
        {1,4,2}, {1,3,4}, {1,5,3}, {1,2,5},
    };
    auto pushBall = [&](glm::vec3 p) {
        triVerts.push_back(p.x); triVerts.push_back(p.y); triVerts.push_back(p.z);
        triVerts.push_back(ballColor.x); triVerts.push_back(ballColor.y); triVerts.push_back(ballColor.z);
    };
    for (auto& f : octFaces) {
        pushBall(bv[f[0]]); pushBall(bv[f[1]]); pushBall(bv[f[2]]);
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
    axesVao_.release();

    // 启用深度测试，确保近处物体遮挡远处物体
    glEnable(GL_DEPTH_TEST);

    // 启动 FPS 计时器
    fpsTimer_.start();

    // 上传初始网格数据到 GPU
    uploadMesh();

    // 通知外部 GL 已初始化（MonitorPanel 会在此时读取硬件信息）
    emit glInitialized();
}

void GLWidget::paintGL() {
    // 恢复 GL 状态（QPainter 可能在上一帧末尾修改了这些）
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    // 如果网格数据有更新，重新上传到 GPU
    if (needsUpload_) uploadMesh();

    // 如果部件可见性有变化，重建并重新上传 IBO
    if (partVisibilityDirty_ && !allTriIndices_.empty()) {
        partVisibilityDirty_ = false;
        std::vector<unsigned int> filtered;
        filtered.reserve(allTriIndices_.size());
        int triCount = static_cast<int>(allTriIndices_.size() / 3);
        for (int t = 0; t < triCount; ++t) {
            int part = (t < static_cast<int>(triToPart_.size())) ? triToPart_[t] : -1;
            if (part >= 0) {
                auto it = partVisibility_.find(part);
                if (it != partVisibility_.end() && !it->second)
                    continue;   // 该部件不可见，跳过
            }
            filtered.push_back(allTriIndices_[t * 3]);
            filtered.push_back(allTriIndices_[t * 3 + 1]);
            filtered.push_back(allTriIndices_[t * 3 + 2]);
        }
        activeIndexCount_ = static_cast<int>(filtered.size());
        vao_.bind();
        ibo_->bind();
        ibo_->allocate(filtered.data(),
                       static_cast<int>(filtered.size() * sizeof(unsigned int)));
        vao_.release();
    }

    if (needsColorUpload_) uploadColors();
    if (edgeVisibilityDirty_) rebuildEdgeIbo();

    // 浅灰背景（参考 ABAQUS/ANSYS 风格）
    glClearColor(0.85f, 0.85f, 0.88f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 无数据时只绘制坐标轴（三角面和边线都没有才算无数据）
    if (mesh_.indices.empty() && mesh_.edgeIndices.empty()) {
        drawAxesIndicator();
        return;
    }

    // ── 计算变换矩阵 ──
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    // 使用较大的 near 值以获得更高的深度精度，避免薄壳面 Z-fighting
    float nearPlane = cam_.distance * 0.01f;
    float farPlane  = cam_.distance * 10.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, nearPlane, farPlane);
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view * model;
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

    // ── 设置 uniform 变量 ──
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

    // ── 绘制实体面 ──
    vao_.bind();
    int count = activeIndexCount_;

    if (count > 0) {
        shader_->setUniformValue("uColor", QVector3D(color_.x, color_.y, color_.z));
        shader_->setUniformValue("uWireframe", false);
        shader_->setUniformValue("uUseVertexColor", !partColors_.empty());
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // ── 绘制网格线 ──
    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uUseVertexColor", false);

    if (activeEdgeIndexCount_ > 0) {
        // 纯 1D 模型用粗线显示，混合模型用细线
        float lineW = (count == 0) ? 3.0f : 1.0f;
        shader_->setUniformValue("uColor", (count == 0)
            ? QVector3D(color_.x, color_.y, color_.z)   // 1D 模型用主色
            : QVector3D(0.2f, 0.2f, 0.22f));            // 混合模型用深色线框
        glLineWidth(lineW);
        vao_.release();
        edgeVao_.bind();
        glDrawElements(GL_LINES, activeEdgeIndexCount_, GL_UNSIGNED_INT, nullptr);
        edgeVao_.release();
        glLineWidth(1.0f);
    } else if (count > 0) {
        shader_->setUniformValue("uColor", QVector3D(0.2f, 0.2f, 0.22f));
        shader_->setUniformValue("uUseVertexColor", false);
        glLineWidth(1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        vao_.release();
    }
    // 网格线绘制完毕

    // ── 高亮选中内容（最后绘制以覆盖普通线框） ──
    if (selectionDirty_) {
        std::vector<float> hlVerts;
        int hlMode = 0;  // 0=lines, 1=points

        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            // 单元模式：完整单元边线
            rebuildSelectionEdges();
            // selEdgeVao_ 已由 rebuildSelectionEdges 更新
            hlMode = 0;
        } else if (!selectedFaces_.empty() && !triToElem_.empty() && !triToFace_.empty()) {
            // 面模式：通过位置匹配从线框边中筛选属于选中面的边
            // 1. 收集选中面所有顶点的位置
            using Pos3 = std::tuple<float,float,float>;
            std::set<Pos3> facePositions;
            int triCount = static_cast<int>(triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                auto fk = std::make_pair(triToElem_[t], triToFace_[t]);
                if (selectedFaces_.count(fk) == 0) continue;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = mesh_.indices[t * 3 + v];
                    facePositions.insert({mesh_.vertices[vi * 6],
                                          mesh_.vertices[vi * 6 + 1],
                                          mesh_.vertices[vi * 6 + 2]});
                }
            }
            // 2. 从线框边数据中筛选两个端点都在面上的边
            int edgeCount = static_cast<int>(mesh_.edgeIndices.size() / 2);
            for (int i = 0; i < edgeCount; ++i) {
                int base = i * 6;
                Pos3 pa = {mesh_.edgeVertices[base], mesh_.edgeVertices[base+1], mesh_.edgeVertices[base+2]};
                Pos3 pb = {mesh_.edgeVertices[base+3], mesh_.edgeVertices[base+4], mesh_.edgeVertices[base+5]};
                if (facePositions.count(pa) && facePositions.count(pb)) {
                    for (int j = 0; j < 6; ++j)
                        hlVerts.push_back(mesh_.edgeVertices[base + j]);
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
            hlMode = 0;
        } else if (!selection_.selectedNodes.empty()) {
            // 节点模式：圆点
            for (int vi : selection_.selectedNodes) {
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
            hlMode = 1;
        }
        selectionDirty_ = false;
        selHlMode_ = hlMode;
    }

    if (selEdgeVertCount_ > 0 && (selection_.hasSelection() || !selectedFaces_.empty())) {
        shader_->setUniformValue("uColor", QVector3D(1.0f, 0.78f, 0.0f));
        shader_->setUniformValue("uWireframe", true);
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

    shader_->release();

    // ── 绘制坐标轴指示器 ──
    drawAxesIndicator();

    // ── FPS 统计 ──
    frameCount_++;
    qint64 elapsed = fpsTimer_.elapsed();
    if (elapsed >= 500) {
        fps_ = frameCount_ * 1000.0f / elapsed;
        frameTime_ = elapsed / static_cast<float>(frameCount_);
        frameCount_ = 0;
        fpsTimer_.restart();
    }
}

void GLWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);

    // 重建拾取 FBO（尺寸需与视口一致）
    int dpr = devicePixelRatio();
    delete pickFbo_;
    makeCurrent();
    pickFbo_ = new QOpenGLFramebufferObject(w * dpr, h * dpr,
        QOpenGLFramebufferObject::Depth);
    doneCurrent();
}

// ============================================================
// 鼠标与键盘事件
// ============================================================

void GLWidget::mousePressEvent(QMouseEvent* e) {
    pressPos_ = e->pos();
    lastPos_ = e->pos();
    isDragging_ = false;
    isBoxSelecting_ = false;

    // Shift + 左键 → 开始框选
    if ((e->button() == Qt::LeftButton) && (e->modifiers() & Qt::ShiftModifier)) {
        isBoxSelecting_ = true;
        boxOrigin_ = e->pos();
        if (!rubberBand_)
            rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
        rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
        rubberBand_->show();
    }
}

void GLWidget::mouseMoveEvent(QMouseEvent* e) {
    // 框选模式：更新矩形
    if (isBoxSelecting_ && rubberBand_) {
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

    if (e->buttons() & Qt::LeftButton)                              cam_.rotate(dx, dy);
    if (e->buttons() & (Qt::RightButton | Qt::MiddleButton))        cam_.pan(dx, dy);

    update();
}

void GLWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (isBoxSelecting_ && rubberBand_) {
            // 框选完成
            rubberBand_->hide();
            QRect rect = QRect(boxOrigin_, e->pos()).normalized();
            if (rect.width() > 3 && rect.height() > 3) {
                pickInRect(rect);
            }
            isBoxSelecting_ = false;
        } else if (!isDragging_) {
            // 点选（未拖拽）
            bool ctrlHeld = (e->modifiers() & Qt::ControlModifier);
            pickAtPoint(e->pos(), ctrlHeld);
        }
    }
    isDragging_ = false;
}

void GLWidget::wheelEvent(QWheelEvent* e) {
    // 按住中键或右键拖动时忽略滚轮，防止平移与缩放同时触发
    if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) return;
    cam_.zoom(e->angleDelta().y() / 120.0f);
    update();
}

void GLWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        if (selection_.hasSelection() || !selectedFaces_.empty()) {
            selection_.clear();
            selectedFaces_.clear();
            selectionDirty_ = true;
            selEdgeVertCount_ = 0;
            emit selectionChanged(0);
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

    pickFbo_->bind();

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    pickShader_->bind();
    pickShader_->setUniformValue("uMVP", QMatrix4x4(glm::value_ptr(glm::transpose(mvp))));
    // (pick shader uses standard depth)

    vao_.bind();

    // 逐单元绘制，每个单元用唯一颜色
    int triCount = static_cast<int>(triToElem_.size());
    int i = 0;
    while (i < triCount) {
        int elemId = triToElem_[i];
        int start = i;
        // 找出同一单元的连续三角形
        while (i < triCount && triToElem_[i] == elemId) ++i;

        glm::vec3 c = idToColor(elemId);
        pickShader_->setUniformValue("uPickColor", QVector3D(c.x, c.y, c.z));
        glDrawElements(GL_TRIANGLES, (i - start) * 3, GL_UNSIGNED_INT,
                       reinterpret_cast<void*>(start * 3 * sizeof(unsigned int)));
    }

    vao_.release();
    pickShader_->release();
    pickFbo_->release();
}

void GLWidget::pickAtPoint(const QPoint& pos, bool ctrlHeld) {
    if (!pickFbo_ || triToElem_.empty()) return;

    makeCurrent();

    // 渲染拾取缓冲
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, cam_.distance * 0.01f, cam_.distance * 10.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;
    renderPickBuffer(mvp);

    // 读取点击位置像素
    pickFbo_->bind();
    int dpr = devicePixelRatio();
    int px = pos.x() * dpr;
    int py = (height() - pos.y()) * dpr;  // OpenGL Y 轴翻转
    unsigned char pixel[4] = {0};
    glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    pickFbo_->release();

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
                        closestNode = static_cast<int>(vi);
                    }
                }
            }
        }
        if (!ctrlHeld) {
            selection_.clear(); selectedFaces_.clear();
            if (closestNode >= 0) selection_.selectedNodes.insert(closestNode);
        } else {
            if (closestNode >= 0) selection_.toggleNode(closestNode);
        }

    } else if (pickMode_ == PickMode::Face) {
        // ── 面拾取：找到点击的三角形所属的面 ──
        int hitFace = -1;
        if (elemId >= 0 && !triToFace_.empty()) {
            // 找到点击位置对应的三角形索引
            // 重新读取像素得到的是 elemId，需要遍历找到具体三角形
            float ndcX = (2.0f * pos.x() / width()) - 1.0f;
            float ndcY = 1.0f - (2.0f * pos.y() / height());
            float minDist2 = 1e30f;
            int triCount = static_cast<int>(triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (triToElem_[t] != elemId) continue;
                // 计算三角形中心的屏幕坐标
                glm::vec3 center(0);
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = mesh_.indices[t * 3 + v];
                    center += glm::vec3(mesh_.vertices[vi * 6],
                                        mesh_.vertices[vi * 6 + 1],
                                        mesh_.vertices[vi * 6 + 2]);
                }
                center /= 3.0f;
                glm::vec4 clip = mvp * glm::vec4(center, 1.0f);
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                float d2 = (sx - ndcX) * (sx - ndcX) + (sy - ndcY) * (sy - ndcY);
                if (d2 < minDist2) {
                    minDist2 = d2;
                    hitFace = triToFace_[t];
                }
            }
        }
        auto faceKey = std::make_pair(elemId, hitFace);
        if (!ctrlHeld) {
            selection_.clear(); selectedFaces_.clear();
            if (elemId >= 0 && hitFace >= 0) selectedFaces_.insert(faceKey);
        } else {
            if (elemId >= 0 && hitFace >= 0) {
                if (selectedFaces_.count(faceKey)) selectedFaces_.erase(faceKey);
                else selectedFaces_.insert(faceKey);
            }
        }

    } else {
        // ── 单元拾取 ──
        if (!ctrlHeld) {
            selection_.clear(); selectedFaces_.clear();
            if (elemId >= 0) selection_.selectedElements.insert(elemId);
        } else {
            if (elemId >= 0) selection_.toggleElement(elemId);
        }
    }

    selectionDirty_ = true;
    emit selectionChanged(selection_.selectedElementCount() + selection_.selectedNodeCount());
    doneCurrent();
    update();
}

void GLWidget::pickInRect(const QRect& rect) {
    if (!pickFbo_ || triToElem_.empty()) return;

    makeCurrent();

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, cam_.distance * 0.01f, cam_.distance * 10.0f);
    glm::mat4 mvp = projection * cam_.viewMatrix();
    renderPickBuffer(mvp);

    // 读取矩形区域内的像素
    pickFbo_->bind();
    int dpr = devicePixelRatio();
    int x = rect.x() * dpr;
    int y = (height() - rect.y() - rect.height()) * dpr;
    int w = rect.width() * dpr;
    int h = rect.height() * dpr;

    // 限制范围
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) { pickFbo_->release(); doneCurrent(); return; }

    std::vector<unsigned char> pixels(w * h * 4);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    pickFbo_->release();

    // 收集命中的单元 ID
    std::unordered_set<int> hitElems;
    for (int i = 0; i < w * h; ++i) {
        int elemId = colorToId(pixels[i * 4], pixels[i * 4 + 1], pixels[i * 4 + 2]);
        if (elemId >= 0) hitElems.insert(elemId);
    }

    selection_.clear();
    selectedFaces_.clear();

    if (pickMode_ == PickMode::Node) {
        // 节点模式：框内所有可见单元的顶点，筛选屏幕坐标在框内的
        float ndcL = (2.0f * rect.left() / width()) - 1.0f;
        float ndcR = (2.0f * rect.right() / width()) - 1.0f;
        float ndcT = 1.0f - (2.0f * rect.top() / height());
        float ndcB = 1.0f - (2.0f * rect.bottom() / height());
        if (ndcL > ndcR) std::swap(ndcL, ndcR);
        if (ndcB > ndcT) std::swap(ndcB, ndcT);

        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (hitElems.count(triToElem_[t]) == 0) continue;
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
                    selection_.selectedNodes.insert(static_cast<int>(vi));
                }
            }
        }
    } else if (pickMode_ == PickMode::Face) {
        // 面模式：框内所有可见的面
        for (int elemId : hitElems) {
            int triCount = static_cast<int>(triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (triToElem_[t] != elemId) continue;
                if (t < static_cast<int>(triToFace_.size())) {
                    selectedFaces_.insert({elemId, triToFace_[t]});
                }
            }
        }
    } else {
        // 单元模式
        selection_.selectedElements = hitElems;
    }

    selectionDirty_ = true;
    emit selectionChanged(static_cast<int>(
        selection_.selectedElementCount() + selection_.selectedNodeCount() + selectedFaces_.size()));
    doneCurrent();
    update();
}

// ============================================================
// 私有方法
// ============================================================

void GLWidget::rebuildSelectionEdges() {
    // 从完整单元边线数据中筛选属于选中单元的边
    int edgeCount = static_cast<int>(mesh_.elemEdgeToElement.size());
    std::vector<float> verts;

    for (int i = 0; i < edgeCount; ++i) {
        if (!selection_.isElementSelected(mesh_.elemEdgeToElement[i])) continue;

        // 每条边 2 顶点 × 3 float = 6 float
        int base = i * 6;
        for (int j = 0; j < 6; ++j) {
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
}

void GLWidget::drawAxesIndicator() {
    const int axesSize = 100;
    const int margin = 10;
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

    // 绘制轴杆（线段）
    glLineWidth(2.5f);
    glDrawArrays(GL_LINES, 0, axesLineCount_);

    // 绘制箭头圆锥 + 中心球（三角面）
    glDrawArrays(GL_TRIANGLES, axesLineCount_, axesTriCount_);

    axesVao_.release();
    axesShader_->release();

    // 恢复主视口
    glViewport(0, 0, width() * dpr, height() * dpr);

    // ── QPainter 绘制轴标签 ──
    auto project = [&](glm::vec3 pt) -> QPointF {
        glm::vec4 clip = axesMVP * glm::vec4(pt, 1.0f);
        float sx = margin + (clip.x / clip.w * 0.5f + 0.5f) * axesSize;
        float sy = height() - margin - (clip.y / clip.w * 0.5f + 0.5f) * axesSize;
        return QPointF(sx, sy);
    };

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    struct AxisLabel { glm::vec3 dir; QString name; QColor color; };
    AxisLabel labels[] = {
        {{1,0,0}, "X", QColor(240, 70, 70)},
        {{0,1,0}, "Y", QColor(70, 200, 70)},
        {{0,0,1}, "Z", QColor(70, 120, 240)},
    };

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(13);
    painter.setFont(font);

    for (const auto& l : labels) {
        QPointF pos = project(l.dir * 1.2f);
        painter.setPen(l.color);
        painter.drawText(QRectF(pos.x() - 10, pos.y() - 10, 20, 20),
                         Qt::AlignCenter, l.name);
    }

    painter.end();
}

void GLWidget::setTriangleToPartMap(const std::vector<int>& map) {
    triToPart_ = map;
    // Assign palette colors to parts
    int numParts = 0;
    for (int p : map) if (p >= 0) numParts = std::max(numParts, p + 1);
    partColors_.resize(numParts);
    for (int i = 0; i < numParts; ++i)
        partColors_[i] = kPartPalette[i % kPartPaletteSize];
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
    update();
}

void GLWidget::uploadColors() {
    needsColorUpload_ = false;
    int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
    if (vertCount == 0 || triToPart_.empty()) return;

    // 通过 allTriIndices_ 建立 顶点 → 部件 反向映射
    std::vector<int> vertexPart(vertCount, -1);
    int triCount = static_cast<int>(allTriIndices_.size() / 3);
    for (int t = 0; t < triCount && t < static_cast<int>(triToPart_.size()); ++t) {
        int part = triToPart_[t];
        for (int k = 0; k < 3; ++k) {
            unsigned int v = allTriIndices_[t * 3 + k];
            if (static_cast<int>(v) < vertCount)
                vertexPart[v] = part;
        }
    }

    std::vector<float> colors(vertCount * 3);
    for (int v = 0; v < vertCount; ++v) {
        int part = vertexPart[v];
        glm::vec3 c = (part >= 0 && part < static_cast<int>(partColors_.size()))
                      ? partColors_[part] : color_;
        colors[v * 3 + 0] = c.x;
        colors[v * 3 + 1] = c.y;
        colors[v * 3 + 2] = c.z;
    }

    colorVbo_.bind();
    colorVbo_.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));
    colorVbo_.release();
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

    vao_.bind();  // 绑定 VAO，后续的 VBO/IBO 绑定和属性配置都会记录在 VAO 中

    // 上传顶点数据到 VBO
    vbo_.bind();
    vbo_.allocate(mesh_.vertices.data(),
                  static_cast<int>(mesh_.vertices.size() * sizeof(float)));

    // 上传索引数据到 IBO
    activeIndexCount_ = static_cast<int>(mesh_.indices.size());
    ibo_->bind();
    ibo_->allocate(mesh_.indices.data(),
                   static_cast<int>(mesh_.indices.size() * sizeof(unsigned int)));

    // 配置顶点属性指针
    // 属性 0：位置 (location=0)，3 个 float，步长 6*float，偏移 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    // 属性 1：法线 (location=1)，3 个 float，步长 6*float，偏移 3*float
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // ── 颜色缓冲（per-vertex 部件颜色，默认为 color_） ──
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
        // 属性 2：部件颜色 (location=2)，3 个 float，步长 3*float，偏移 0
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(2);
    }

    vao_.release();  // 解绑 VAO

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

        edgeVao_.release();
    }
}
