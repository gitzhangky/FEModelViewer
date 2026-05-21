#pragma once

#include <QPoint>
#include <QRect>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFramebufferObject>
#include <glm/glm.hpp>
#include <unordered_set>

class GLWidget;
class QOpenGLShaderProgram;

class PickRenderer {
public:
    explicit PickRenderer(GLWidget& w) : w_(w) {}
    ~PickRenderer();

    void initGL();
    void resizeFbo(int w, int h, int dpr);

    void processDeferredPicks();
    void requestPickPoint(const QPoint& pos, bool ctrlHeld);
    void requestPickRect(const QRect& rect, bool ctrlHeld);
    void requestDeselectPoint(const QPoint& pos);
    void requestDeselectRect(const QRect& rect);

    // 部件级选择（供外部调用）
    void selectPart(int partIndex);
    void deselectPart(int partIndex);
    bool isPartFullySelected(int partIndex) const;

private:
    void renderPickBuffer(const glm::mat4& mvp);
    void pickAtPoint(const QPoint& pos, bool ctrlHeld);
    void pickInRect(const QRect& rect, bool ctrlHeld);
    void deselectAtPoint(const QPoint& pos);
    void deselectInRect(const QRect& rect);

    glm::vec3 idToColor(int id);
    int colorToId(unsigned char r, unsigned char g, unsigned char b);

    glm::mat4 buildPickMvp() const;
    void emitSelectionSignals();

    // 渲染拾取缓冲并扫描矩形内出现的单元 ID（遵循遮挡，只得到可见最前表层单元）
    void collectVisibleElemsInRect(const QRect& rect, const glm::mat4& mvp,
                                   std::unordered_set<int>& out);

    GLWidget& w_;
    QOpenGLShaderProgram* pickShader_ = nullptr;
    QOpenGLFramebufferObject* pickFbo_ = nullptr;
    QOpenGLVertexArrayObject pickVao_;

    // 延迟拾取状态（由 GLWidget 鼠标事件设置，paintGL 内执行）
    bool pickPointPending_ = false;
    QPoint pendingPickPos_;
    bool pendingPickCtrl_ = false;
    bool pickRectPending_ = false;
    QRect pendingPickRect_;
    bool deselectPointPending_ = false;
    QPoint pendingDeselectPos_;
    bool deselectRectPending_ = false;
    QRect pendingDeselectRect_;
};
