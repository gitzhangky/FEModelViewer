#include <QApplication>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QRubberBand>
#include <QShortcut>
#include <QTimer>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Camera.h"
#include "FEPickResult.h"
#include "Geometry.h"
#include "ferender_export.h"

#define private public
#define protected public
#include "GLWidget.h"
#undef protected
#undef private

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static Mesh makeTriangleMesh()
{
    Mesh mesh;
    mesh.vertices = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    mesh.indices = {0, 1, 2};
    return mesh;
}

static bool closeVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
{
    return glm::length(a - b) <= eps;
}

static void testDoubleClickQueuesPivotOnlyForLeftButtonOnTriangleMesh()
{
    GLWidget widget;

    QMouseEvent emptyMeshEvent(QEvent::MouseButtonDblClick,
                               QPointF(12.0, 18.0),
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
    widget.mouseDoubleClickEvent(&emptyMeshEvent);
    require(!widget.pivotPending_, "empty mesh does not queue a pivot request");

    widget.setMesh(makeTriangleMesh());
    QMouseEvent rightButtonEvent(QEvent::MouseButtonDblClick,
                                 QPointF(21.0, 31.0),
                                 Qt::RightButton,
                                 Qt::RightButton,
                                 Qt::NoModifier);
    widget.mouseDoubleClickEvent(&rightButtonEvent);
    require(!widget.pivotPending_, "right double-click does not queue a pivot request");

    QMouseEvent leftButtonEvent(QEvent::MouseButtonDblClick,
                                QPointF(42.0, 55.0),
                                Qt::LeftButton,
                                Qt::LeftButton,
                                Qt::NoModifier);
    widget.mouseDoubleClickEvent(&leftButtonEvent);
    require(widget.pivotPending_, "left double-click on triangle mesh queues a pivot request");
    require(widget.pendingPivotPos_ == QPoint(42, 55), "queued pivot stores the double-click position");

    std::printf("  PASS: double-click queues pivot only for valid left-button mesh clicks\n");
}

static void testFitToModelAndSpaceResetPivot()
{
    GLWidget widget;

    const glm::vec3 modelCenter(1.0f, 2.0f, 3.0f);
    widget.fitToModel(modelCenter, 8.0f);
    require(widget.hasModelCenter_, "fitToModel records the model center");
    require(!widget.hasCustomPivot_, "fitToModel clears a custom pivot");
    require(closeVec(widget.modelCenter_, modelCenter), "stored model center matches fitToModel center");

    widget.hasCustomPivot_ = true;
    widget.pivotMarkerActive_ = true;
    widget.pivotResetHintActive_ = false;
    widget.cam_.target = glm::vec3(-5.0f, 4.0f, 9.0f);

    QShortcut* spaceShortcut = nullptr;
    for (auto* shortcut : widget.findChildren<QShortcut*>()) {
        if (shortcut->key() == QKeySequence(Qt::Key_Space)) {
            spaceShortcut = shortcut;
            break;
        }
    }
    require(spaceShortcut != nullptr, "space shortcut is registered");

    bool invoked = QMetaObject::invokeMethod(spaceShortcut, "activated", Qt::DirectConnection);
    require(invoked, "space shortcut activation can be invoked");
    require(!widget.hasCustomPivot_, "space reset clears custom pivot");
    require(!widget.pivotMarkerActive_, "space reset hides pivot marker");
    require(widget.pivotResetHintActive_, "space reset enables reset hint");
    require(closeVec(widget.cam_.target, modelCenter), "space reset restores camera target to model center");

    std::printf("  PASS: Space resets custom pivot to the model center\n");
}

static void testOrbitAroundPivotKeepsTargetRadius()
{
    GLWidget widget;
    widget.cam_.target = glm::vec3(3.0f, 1.5f, -2.0f);
    widget.cam_.yaw = 20.0f;
    widget.cam_.pitch = 15.0f;
    widget.orbitPivot_ = glm::vec3(1.0f, -1.0f, 0.5f);

    const float before = glm::length(widget.cam_.target - widget.orbitPivot_);
    widget.orbitAroundPivot(80.0f, -40.0f);
    const float after = glm::length(widget.cam_.target - widget.orbitPivot_);

    require(std::abs(before - after) < 1e-4f, "orbiting around custom pivot preserves target radius");
    require(std::abs(widget.cam_.yaw - 8.0f) < 1e-4f, "orbiting updates camera yaw");
    require(std::abs(widget.cam_.pitch - 9.0f) < 1e-4f, "orbiting updates camera pitch");

    std::printf("  PASS: custom-pivot orbit keeps target on the pivot sphere\n");
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("=== Pivot Interaction Tests ===\n");
    testDoubleClickQueuesPivotOnlyForLeftButtonOnTriangleMesh();
    testFitToModelAndSpaceResetPivot();
    testOrbitAroundPivotKeepsTargetRadius();
    std::printf("All Pivot Interaction tests passed!\n");
    return 0;
}
