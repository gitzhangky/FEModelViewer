#pragma once

#include <glm/glm.hpp>

#include <unordered_set>
#include <vector>

class GLWidget;
class QPainter;

class LabelOverlay {
public:
    explicit LabelOverlay(GLWidget& w);

    void setAxesMvp(const glm::mat4& mvp);
    void drawAxesLabels(QPainter& painter);
    void drawIdLabels(QPainter& painter, const glm::mat4& mvp);
    void render(QPainter& painter, const glm::mat4& sceneMvp);

private:
    GLWidget& w_;
    glm::mat4 axesMVP_{1.0f};
    std::unordered_set<long long> labelBinScratch_;
    int lastLabelMode_ = -1;
    std::vector<int> lastLabelIds_;
};
