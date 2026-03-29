/**
 * @file GLWidget.cpp
 * @brief OpenGL 渲染窗口组件实现
 */

#include "GLWidget.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFontMetrics>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>

// ============================================================
// ColorBarOverlay — 独立于 GL 的色标覆盖层
//
// 作为 GLWidget 的子控件，使用 Qt raster 绘图引擎绘制色标。
// 完全不涉及 OpenGL 状态，从根本上避免拾取等 GL 操作
// 导致 QPainter(QOpenGLWidget) 绘图失败的问题。
// ============================================================

class ColorBarOverlay : public QWidget {
public:
    explicit ColorBarOverlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setVisible(false);
    }

    void setRange(float mn, float mx) { min_ = mn; max_ = mx; update(); }
    void setTitle(const QString& t) { title_ = t; update(); }
    void setTextColor(const QColor& c) { textColor_ = c; update(); }
    void setExtremes(int minId, float minVal, int maxId, float maxVal) {
        minId_ = minId; minVal_ = minVal;
        maxId_ = maxId; maxVal_ = maxVal;
        hasExtremes_ = true;
        update();
    }
    void setIdLabel(const QString& label) { idLabel_ = label; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        const int segCount = 9;
        const int barW = 20;
        const int segH = 28;
        const int barH = segCount * segH;
        const int margin = 14;
        const int barLabelGap = 8;

        auto jetColor = [](float t) -> QColor {
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float r = 0, g = 0, b = 0;
            if (t < 0.125f)      { r = 0;   g = 0;   b = 0.5f + t / 0.125f * 0.5f; }
            else if (t < 0.375f) { r = 0;   g = (t - 0.125f) / 0.25f; b = 1.0f; }
            else if (t < 0.625f) { r = (t - 0.375f) / 0.25f; g = 1.0f; b = 1.0f - (t - 0.375f) / 0.25f; }
            else if (t < 0.875f) { r = 1.0f; g = 1.0f - (t - 0.625f) / 0.25f; b = 0; }
            else                 { r = 1.0f - (t - 0.875f) / 0.125f * 0.5f; g = 0; b = 0; }
            return QColor(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255));
        };

        auto formatValue = [](float val) -> QString {
            return QString::number(static_cast<double>(val), 'E', 3);
        };

        QFont labelFont("Consolas", 0);
        labelFont.setPixelSize(14);
        QFontMetrics fm(labelFont);
        int labelTextH = fm.height();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(labelFont);

        // 绘制分段色块
        for (int i = 0; i < segCount; ++i) {
            float t = (i + 0.5f) / segCount;
            int y = margin + barH - (i + 1) * segH;
            painter.setPen(Qt::NoPen);
            painter.setBrush(jetColor(t));
            painter.drawRect(margin, y, barW, segH);
        }

        // 段界横线 + 数值标签
        int labelX = margin + barW + barLabelGap;
        for (int i = 0; i <= segCount; ++i) {
            float t = 1.0f - i / static_cast<float>(segCount);
            float val = min_ + (max_ - min_) * t;
            int y = margin + i * segH;

            painter.setPen(QPen(QColor(0, 0, 0), 1));
            painter.drawLine(margin, y, margin + barW, y);

            painter.setPen(textColor_);
            QRectF labelRect(labelX, y - labelTextH / 2, 120, labelTextH);
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, formatValue(val));
        }

        // 色标下方显示最大/最小值及其 ID
        if (hasExtremes_) {
            int infoY = margin + barH + 10;
            QFont infoFont("Consolas", 0);
            infoFont.setPixelSize(12);
            painter.setFont(infoFont);
            painter.setPen(textColor_);

            QString maxLine = QString("Max: %1 (%2: %3)").arg(formatValue(maxVal_)).arg(idLabel_).arg(maxId_);
            QString minLine = QString("Min: %1 (%2: %3)").arg(formatValue(minVal_)).arg(idLabel_).arg(minId_);

            painter.drawText(margin, infoY, 200, 16, Qt::AlignLeft | Qt::AlignVCenter, maxLine);
            painter.drawText(margin, infoY + 18, 200, 16, Qt::AlignLeft | Qt::AlignVCenter, minLine);
        }

        painter.end();
    }

private:
    float min_ = 0.0f, max_ = 1.0f;
    QString title_ = "Result";
    QColor textColor_{30, 30, 30};
    bool hasExtremes_ = false;
    int minId_ = -1, maxId_ = -1;
    float minVal_ = 0.0f, maxVal_ = 0.0f;
    QString idLabel_ = "ID";  // "Node ID" 或 "Ele ID"
};

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
layout (location = 3) in float aScalar;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out float vScalar;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal   = normalize(uNormalMat * aNormal);
    vColor    = aColor;
    vScalar   = aScalar;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

/**
 * 片段着色器（改进光照模型）：
 *   - 线框模式：直接输出纯色
 *   - 光照模式：主光 + 补光 + 环境光 + 高光
 *   - 部件颜色：用 gl_PrimitiveID 查 texture buffer 获取部件索引
 */
static const char* fragSrc = R"(
#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in float vScalar;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uColor;
uniform bool uWireframe;
uniform float uWireAlpha;
uniform bool uUseVertexColor;
uniform bool uContourMode;
uniform float uScalarMin;
uniform float uScalarMax;
uniform int uNumBands;
uniform samplerBuffer uTriPartMap;
out vec4 outColor;

// 部件调色板（与 C++ 端 kPartPalette 一致）
const vec3 kPalette[8] = vec3[8](
    vec3(0.61, 0.86, 0.63),
    vec3(0.54, 0.71, 0.98),
    vec3(0.98, 0.70, 0.53),
    vec3(0.82, 0.62, 0.98),
    vec3(0.58, 0.89, 0.83),
    vec3(0.98, 0.89, 0.69),
    vec3(0.94, 0.56, 0.66),
    vec3(0.71, 0.71, 0.98)
);

vec3 jetColor(float t) {
    t = clamp(t, 0.0, 1.0);
    float r, g, b;
    if (t < 0.125)      { r = 0.0; g = 0.0; b = 0.5 + t/0.125*0.5; }
    else if (t < 0.375) { r = 0.0; g = (t-0.125)/0.25; b = 1.0; }
    else if (t < 0.625) { r = (t-0.375)/0.25; g = 1.0; b = 1.0-(t-0.375)/0.25; }
    else if (t < 0.875) { r = 1.0; g = 1.0-(t-0.625)/0.25; b = 0.0; }
    else                 { r = 1.0-(t-0.875)/0.125*0.5; g = 0.0; b = 0.0; }
    return vec3(r, g, b);
}

void main() {
    vec3 surfaceColor = uUseVertexColor ? vColor : uColor;

    if (uWireframe) {
        outColor = vec4(surfaceColor, uWireAlpha);
        return;
    }

    if (uContourMode) {
        // 云图模式：标量值量化 + Jet colormap
        float range = uScalarMax - uScalarMin;
        float t = (range > 1e-10) ? clamp((vScalar - uScalarMin) / range, 0.0, 1.0) : 0.5;
        int band = int(t * float(uNumBands));
        if (band >= uNumBands) band = uNumBands - 1;
        float qt = (float(band) + 0.5) / float(uNumBands);
        surfaceColor = jetColor(qt);
    } else if (uUseVertexColor) {
        // 部件颜色模式：用 gl_PrimitiveID 查 triToPart texture buffer
        int partIdx = int(texelFetch(uTriPartMap, gl_PrimitiveID).r);
        int idx = partIdx % 8;
        if (idx < 0) idx += 8;
        surfaceColor = kPalette[idx];
    }

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;

    // 主方向光
    vec3 L1 = normalize(-uLightDir);
    float diff1 = max(dot(N, L1), 0.0);

    // 补光（从相反方向来，强度较弱，避免背光面全黑）
    vec3 L2 = normalize(vec3(0.3, 0.5, 0.4));
    float diff2 = max(dot(N, L2), 0.0);

    // Blinn-Phong 高光（主光源）
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L1 + V);

    float ambient, kDiff1, kDiff2, kSpec, shininess;
    if (uContourMode) {
        // 云图模式：高环境光保护色谱颜色，无高光
        ambient = 0.55;
        kDiff1  = 0.35;
        kDiff2  = 0.10;
        kSpec   = 0.0;
        shininess = 32.0;
    } else {
        // 几何模式：柔和高光，避免冲淡部件颜色
        ambient = 0.65;
        kDiff1  = 0.35;
        kDiff2  = 0.20;
        kSpec   = 0.10;
        shininess = 64.0;
    }

    float spec = pow(max(dot(N, H), 0.0), shininess) * kSpec;
    float diffuse = diff1 * kDiff1 + diff2 * kDiff2;
    float sideFactor = gl_FrontFacing ? 1.0 : 0.8;

    vec3 color = surfaceColor * (ambient + diffuse) * sideFactor + vec3(spec * sideFactor);
    // 防止过曝
    color = min(color, vec3(1.0));
    outColor = vec4(color, 1.0);
}
)";

// ============================================================
// 渐变背景着色器
// ============================================================

static const char* bgVertSrc = R"(
#version 410 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* bgFragSrc = R"(
#version 410 core
in vec3 vColor;
out vec4 outColor;
void main() {
    outColor = vec4(vColor, 1.0);
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
    vao_.release();
    update();
}

void GLWidget::setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands) {
    useVertexColor_ = true;
    scalarMin_ = minVal;
    scalarMax_ = maxVal;
    numBands_ = numBands;
    vao_.bind();
    scalarVbo_.bind();
    scalarVbo_.allocate(scalars.data(), static_cast<int>(scalars.size() * sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glEnableVertexAttribArray(3);
    // 恢复 colorVbo_ 的 attribute 2 绑定（防止 scalarVbo_ 的 bind 污染）
    colorVbo_.bind();
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(2);
    vao_.release();
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
    shader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc);
    shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc);
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
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, pickVertSrc);
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, pickFragSrc);
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
    bgShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, bgVertSrc);
    bgShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, bgFragSrc);
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
        bgVao_.release();
    }

    // ── 坐标轴指示器着色器 ──
    axesShader_ = new QOpenGLShaderProgram(this);
    axesShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, axesVertSrc);
    axesShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, axesFragSrc);
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

    // ── 处理延迟拾取请求（全部使用原始 GL 调用，不污染 Qt 状态追踪） ──
    if (pickPointPending_) {
        pickPointPending_ = false;
        pickAtPoint(pendingPickPos_, pendingPickCtrl_);
    }
    if (pickRectPending_) {
        pickRectPending_ = false;
        pickInRect(pendingPickRect_);
    }
    if (deselectPointPending_) {
        deselectPointPending_ = false;
        deselectAtPoint(pendingDeselectPos_);
    }
    if (deselectRectPending_) {
        deselectRectPending_ = false;
        deselectInRect(pendingDeselectRect_);
    }

    // 如果网格数据有更新，重新上传到 GPU
    if (needsUpload_) uploadMesh();

    // 如果部件可见性有变化，重建并重新上传 IBO 及 triPart texture buffer
    if (partVisibilityDirty_ && !allTriIndices_.empty()) {
        partVisibilityDirty_ = false;
        std::vector<unsigned int> filtered;
        filtered.reserve(allTriIndices_.size());
        std::vector<float> filteredTriPart;   // 与 filtered 同步的部件索引
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
            filteredTriPart.push_back(static_cast<float>(part));
        }
        activeIndexCount_ = static_cast<int>(filtered.size());
        vao_.bind();
        ibo_->bind();
        ibo_->allocate(filtered.data(),
                       static_cast<int>(filtered.size() * sizeof(unsigned int)));
        vao_.release();

        // 同步更新 triToPart texture buffer，使 gl_PrimitiveID 与部件索引对齐
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

    if (needsColorUpload_) uploadColors();
    if (edgeVisibilityDirty_) rebuildEdgeIbo();

    // ── 渐变背景（专用着色器 + 预创建 VAO，无状态污染）──
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    bgShader_->bind();
    bgVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    bgVao_.release();
    bgShader_->release();
    glEnable(GL_DEPTH_TEST);

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
    glm::vec3 eyePos = cam_.eye();
    shader_->setUniformValue("uViewPos", QVector3D(eyePos.x, eyePos.y, eyePos.z));
    shader_->setUniformValue("uContourMode", useVertexColor_ && colorBarVisible_);
    shader_->setUniformValue("uScalarMin", scalarMin_);
    shader_->setUniformValue("uScalarMax", scalarMax_);
    shader_->setUniformValue("uNumBands", numBands_);

    // 绑定 triToPart texture buffer 到纹理单元 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, triPartTex_);
    shader_->setUniformValue("uTriPartMap", 0);

    // ── 绘制实体面 ──
    vao_.bind();
    int count = activeIndexCount_;

    if (count > 0) {
        shader_->setUniformValue("uColor", QVector3D(color_.x, color_.y, color_.z));
        shader_->setUniformValue("uWireframe", false);
        shader_->setUniformValue("uUseVertexColor", useVertexColor_ || !partColors_.empty());
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // ── 绘制网格线（密度自适应透明度） ──
    shader_->setUniformValue("uWireframe", true);
    shader_->setUniformValue("uUseVertexColor", false);

    // 计算屏幕空间边密度，密集时自动降低透明度
    float wireAlpha = 1.0f;
    int numTri = activeIndexCount_ / 3;
    if (numTri > 0 && count > 0) {
        // 估算模型大小（fitToModel 中 maxDist = size * 10）
        float modelSize = cam_.maxDist * 0.1f;
        // 估算平均边长 ≈ 模型直径 / sqrt(三角面数)
        float avgEdgeLen = modelSize * 2.0f / std::sqrt(static_cast<float>(numTri));
        // 投影到屏幕像素
        float fovFactor = height() / (2.0f * std::tan(glm::radians(22.5f)));
        float screenEdgePx = avgEdgeLen / cam_.distance * fovFactor;
        // 平滑过渡：< 3px 全透明，> 10px 全不透明
        wireAlpha = glm::clamp((screenEdgePx - 3.0f) / 7.0f, 0.0f, 1.0f);
    }

    if (activeEdgeIndexCount_ > 0) {
        float lineW = (count == 0) ? 3.0f : 1.0f;
        float alpha = (count == 0) ? 1.0f : wireAlpha;  // 纯 1D 模型不淡化
        shader_->setUniformValue("uColor", (count == 0)
            ? QVector3D(color_.x, color_.y, color_.z)
            : QVector3D(0.2f, 0.2f, 0.22f));
        shader_->setUniformValue("uWireAlpha", alpha);
        glLineWidth(lineW);
        if (alpha < 1.0f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        vao_.release();
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
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        vao_.release();
        if (wireAlpha < 1.0f) glDisable(GL_BLEND);
    }
    // 网格线绘制完毕

    // ── 高亮选中内容（最后绘制以覆盖普通线框） ──
    if (selectionDirty_) {
        std::vector<float> hlVerts;
        int hlMode = 0;  // 0=lines, 1=points

        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            // 单元/部件模式：边线高亮
            partEdgeCacheValid_ = false;  // 选中变化，缓存失效
            rebuildSelectionEdges();
            hlMode = 0;
        } else if (!selection_.selectedNodes.empty()) {
            // 节点模式：圆点
            // 构建 FEM 节点 ID → 第一个渲染顶点索引 的反向映射
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
                    vi = nid;  // fallback: 无映射表时直接用 ID 作索引
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
            hlMode = 1;
        }
        selectionDirty_ = false;
        silhouetteDirty_ = false;
        selHlMode_ = hlMode;
    } else if (silhouetteDirty_ && partEdgeCacheValid_ &&
               pickMode_ == PickMode::Part && selection_.hasSelection()) {
        // 仅视角变化：从缓存快速刷新轮廓边
        updateSilhouetteFromCache();
        silhouetteDirty_ = false;
    }

    if (selEdgeVertCount_ > 0 && selection_.hasSelection()) {
        shader_->setUniformValue("uWireframe", true);
        shader_->setUniformValue("uWireAlpha", 1.0f);
        shader_->setUniformValue("uUseVertexColor", false);

        // 画边线或节点
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

    shader_->release();

    // ── 绘制坐标轴指示器（GL 部分，不含 QPainter 标签） ──
    drawAxesIndicator();

    // ── QPainter 阶段：轴标签 + ID 标签（色标已由独立覆盖层控件绘制） ──
    {
        QPainter painter(this);
        painter.beginNativePainting();
        painter.endNativePainting();
        painter.setRenderHint(QPainter::Antialiasing);
        drawAxesLabels(painter);
        if (showLabels_ && selection_.hasSelection())
            drawIdLabels(painter, mvp);
        painter.end();
    }

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
        if (rect.width() > 3 && rect.height() > 3) {
            // 框选
            pickRectPending_ = true;
            pendingPickRect_ = rect;
            update();
        } else {
            // 范围太小视为点选
            pickPointPending_ = true;
            pendingPickPos_ = e->pos();
            pendingPickCtrl_ = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
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

    // ── 获取原始 GL 函数指针，完全绕过 Qt 的 GL 状态追踪 ──
    auto glBindVAO_ = reinterpret_cast<void(APIENTRY*)(GLuint)>(
        context()->getProcAddress("glBindVertexArray"));
    auto glUseProg_ = reinterpret_cast<void(APIENTRY*)(GLuint)>(
        context()->getProcAddress("glUseProgram"));
    auto glGetUniformLoc_ = reinterpret_cast<GLint(APIENTRY*)(GLuint, const char*)>(
        context()->getProcAddress("glGetUniformLocation"));
    auto glUniformMat4_ = reinterpret_cast<void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*)>(
        context()->getProcAddress("glUniformMatrix4fv"));
    auto glUniform3f_ = reinterpret_cast<void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat)>(
        context()->getProcAddress("glUniform3f"));
    auto glGenBuf_ = reinterpret_cast<void(APIENTRY*)(GLsizei, GLuint*)>(
        context()->getProcAddress("glGenBuffers"));
    auto glDelBuf_ = reinterpret_cast<void(APIENTRY*)(GLsizei, const GLuint*)>(
        context()->getProcAddress("glDeleteBuffers"));
    auto glBufData_ = reinterpret_cast<void(APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum)>(
        context()->getProcAddress("glBufferData"));

    if (!glBindVAO_ || !glUseProg_ || !glGetUniformLoc_ || !glUniformMat4_ ||
        !glUniform3f_ || !glGenBuf_ || !glDelBuf_ || !glBufData_) return;

    // ── 保存所有关键 GL 状态 ──
    GLint prevFbo, prevProgram, prevVao, prevABO, prevEBO;
    GLint prevViewport[4];
    GLfloat prevClearColor[4];
    GLboolean prevDepthTest, prevBlend;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevABO);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEBO);
    glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
    glGetBooleanv(GL_BLEND, &prevBlend);

    // ── 全部使用原始 GL 调用，不触碰任何 Qt 包装器 ──
    glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_->handle());

    // 设置 viewport 匹配 FBO 尺寸（QPainter 可能在上一帧修改了 viewport）
    int dpr = devicePixelRatio();
    glViewport(0, 0, width() * dpr, height() * dpr);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    GLuint pickProg = pickShader_->programId();
    glUseProg_(pickProg);

    GLint mvpLoc = glGetUniformLoc_(pickProg, "uMVP");
    glUniformMat4_(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

    GLint pickColorLoc = glGetUniformLoc_(pickProg, "uPickColor");

    glBindVAO_(pickVao_.objectId());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_.bufferId());
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    // 创建原始 GL 索引缓冲
    GLuint rawIbo = 0;
    glGenBuf_(1, &rawIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rawIbo);
    glBufData_(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(allTriIndices_.size() * sizeof(unsigned int)),
               allTriIndices_.data(), GL_STATIC_DRAW);

    // 逐单元绘制，跳过隐藏部件
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
        glUniform3f_(pickColorLoc, c.x, c.y, c.z);
        glDrawElements(GL_TRIANGLES, (i - start) * 3, GL_UNSIGNED_INT,
                       reinterpret_cast<void*>(start * 3 * sizeof(unsigned int)));
    }

    // ── 清理临时 IBO ──
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDelBuf_(1, &rawIbo);

    // ── 恢复所有 GL 状态（同样使用原始调用） ──
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
    glBindVAO_(static_cast<GLuint>(prevVao));
    glUseProg_(static_cast<GLuint>(prevProgram));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevABO));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevEBO));
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

void GLWidget::pickAtPoint(const QPoint& pos, bool ctrlHeld) {
    if (!pickFbo_ || triToElem_.empty()) return;

    // 注意：此函数现在仅在 paintGL() 内调用，GL 上下文已由 Qt 管理，
    // 无需手动 makeCurrent/doneCurrent。

    // 渲染拾取缓冲
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, cam_.distance * 0.01f, cam_.distance * 10.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;

    renderPickBuffer(mvp);

    // 读取点击位置像素（使用原始 GL 调用，避免 Qt FBO 状态追踪污染）
    unsigned char pixel[4] = {0};
    {
        GLint prevFbo;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_->handle());

        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;  // OpenGL Y 轴翻转
        glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
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

void GLWidget::pickInRect(const QRect& rect) {
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

    selection_.clear();

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
        GLint prevFbo;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_->handle());
        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;
        glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
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
        for (int id : ids) selection_.selectedNodes.insert(id);
    } else if (mode == PickMode::Part) {
        for (int pi : ids) selectPart(pi);
    } else {
        for (int id : ids) selection_.selectedElements.insert(id);
    }

    selectionDirty_ = true;
    showLabels_ = true;

    // 发射选中变更信号
    std::vector<int> sortedIds(ids.begin(), ids.end());
    std::sort(sortedIds.begin(), sortedIds.end());
    emit selectionChanged(mode, static_cast<int>(sortedIds.size()), sortedIds);

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

void GLWidget::drawColorBar(QPainter& painter) {
    // ════════════════════════════════════════════════════════════
    // 仿 HyperView 颜色条样式
    //
    // 使用 QImage 离屏渲染后 drawImage 贴图，避免 QPainter 的 GL
    // 图元绘制（drawRect）在拾取后因 GL 状态残留而失败。
    // drawImage 走纹理路径（与 drawText 相同），不受影响。
    // ════════════════════════════════════════════════════════════

    const int segCount = 9;
    const int barW = 20;              // 色块宽度
    const int segH = 28;             // 每段高度
    const int barH = segCount * segH;
    const int margin = 14;
    const int barLabelGap = 8;        // 色块与标签间距

    // Jet colormap 采样
    auto jetColor = [](float t) -> QColor {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float r = 0, g = 0, b = 0;
        if (t < 0.125f)      { r = 0;   g = 0;   b = 0.5f + t / 0.125f * 0.5f; }
        else if (t < 0.375f) { r = 0;   g = (t - 0.125f) / 0.25f; b = 1.0f; }
        else if (t < 0.625f) { r = (t - 0.375f) / 0.25f; g = 1.0f; b = 1.0f - (t - 0.375f) / 0.25f; }
        else if (t < 0.875f) { r = 1.0f; g = 1.0f - (t - 0.625f) / 0.25f; b = 0; }
        else                 { r = 1.0f - (t - 0.875f) / 0.125f * 0.5f; g = 0; b = 0; }
        return QColor(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255));
    };

    // 格式化函数：HyperView 风格 (X.XXXE+XX)
    auto formatValue = [](float val) -> QString {
        return QString::number(static_cast<double>(val), 'E', 3);
    };

    // 字体
    QFont labelFont("Consolas", 0);
    labelFont.setPixelSize(14);
    QFontMetrics fm(labelFont);
    int labelTextH = fm.height();

    // 计算整体尺寸（色块 + 标签）
    int totalW = barW + barLabelGap + 120;
    int totalH = barH + labelTextH;

    // ── 离屏渲染到 QImage ──
    QImage img(totalW, totalH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter ip(&img);
        ip.setRenderHint(QPainter::Antialiasing);
        ip.setFont(labelFont);

        // 绘制分段色块
        for (int i = 0; i < segCount; ++i) {
            float t = (i + 0.5f) / segCount;
            int y = barH - (i + 1) * segH;
            ip.setPen(Qt::NoPen);
            ip.setBrush(jetColor(t));
            ip.drawRect(0, y, barW, segH);
        }

        // 段界横线 + 数值标签
        int labelX = barW + barLabelGap;
        for (int i = 0; i <= segCount; ++i) {
            float t = 1.0f - i / static_cast<float>(segCount);
            float val = colorBarMin_ + (colorBarMax_ - colorBarMin_) * t;
            int y = i * segH;

            // 段界横线（黑色）
            ip.setPen(QPen(QColor(0, 0, 0), 1));
            ip.drawLine(0, y, barW, y);

            // 数值标签（颜色随主题变化）
            ip.setPen(barTextColor_);
            QRectF labelRect(labelX, y - labelTextH / 2, 120, labelTextH);
            ip.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, formatValue(val));
        }
    }

    // ── 贴图到 GL 画面（走纹理路径，不受 GL 状态污染影响） ──
    painter.drawImage(margin, margin, img);
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

        edgeVao_.release();
    }
}
