/**
 * @file test_post_state.cpp
 * @brief PostState 状态转换单元测试
 *
 * 测试 DeformState / ContourState / PostEffectState 的状态查询方法
 * 和 MainWindow 用到的状态组合逻辑（activeRenderData / displayRenderData）。
 */

#include "PostState.h"
#include <cassert>
#include <cstdio>

// ── 辅助：构建简单的渲染数据用于状态测试 ──

static FERenderData makeSimpleRD(int triCount) {
    FERenderData rd;
    for (int i = 0; i < triCount; ++i) {
        unsigned int base = static_cast<unsigned int>(i * 3);
        for (int v = 0; v < 3; ++v) {
            float x = static_cast<float>(base + v);
            rd.mesh.vertices.insert(rd.mesh.vertices.end(), {x, 0, 0, 0, 0, 1});
            rd.mesh.indices.push_back(base + v);
        }
        rd.triangleToElement.push_back(100 + i);
        rd.triangleToFace.push_back(0);
        rd.triangleToPart.push_back(0);
    }
    rd.vertexToNode.resize(triCount * 3, 1);
    return rd;
}

// 模拟 MainWindow::activeRenderData 逻辑
static const FERenderData& activeRenderData(const DeformState& deform,
                                             const FERenderData& baseRD) {
    return deform.active ? deform.renderData : baseRD;
}

// 模拟 MainWindow::displayRenderData 逻辑
static const FERenderData& displayRenderData(const PostEffectState& post,
                                              const DeformState& deform,
                                              const FERenderData& baseRD) {
    if (post.isActive() && post.isMeshReplacement())
        return post.filteredRD;
    return activeRenderData(deform, baseRD);
}

// ════════════════════════════════════════════════════════════
// PostEffectState 基础查询
// ════════════════════════════════════════════════════════════

static void testInitialState() {
    PostEffectState s;
    assert(s.mode == PostEffectMode::None);
    assert(!s.applied);
    assert(!s.isActive());
    assert(!s.isMeshReplacement());
    assert(!s.isOverlay());
    printf("  PASS: initial state\n");
}

static void testThresholdIsReplacement() {
    PostEffectState s;
    s.mode = PostEffectMode::Threshold;
    s.applied = true;
    s.threshold = {"Stress", 100.0f, 500.0f};

    assert(s.isActive());
    assert(s.isMeshReplacement());
    assert(!s.isOverlay());
    printf("  PASS: threshold is mesh replacement\n");
}

static void testClipPlaneIsReplacement() {
    PostEffectState s;
    s.mode = PostEffectMode::ClipPlane;
    s.applied = true;
    s.clipPlane = {1, 5.0f, glm::vec3(0, 5, 0), glm::vec3(0, 1, 0), true};

    assert(s.isActive());
    assert(s.isMeshReplacement());
    assert(!s.isOverlay());
    printf("  PASS: clip plane is mesh replacement\n");
}

static void testSliceIsOverlay() {
    PostEffectState s;
    s.mode = PostEffectMode::Slice;
    s.applied = true;
    s.slice = {2, 10.0f, glm::vec3(0, 0, 10), glm::vec3(0, 0, 1)};

    assert(s.isActive());
    assert(!s.isMeshReplacement());
    assert(s.isOverlay());
    printf("  PASS: slice is overlay\n");
}

static void testIsoSurfaceIsOverlay() {
    PostEffectState s;
    s.mode = PostEffectMode::IsoSurface;
    s.applied = true;
    s.iso = {"Temperature", 50.0f};

    assert(s.isActive());
    assert(!s.isMeshReplacement());
    assert(s.isOverlay());
    printf("  PASS: iso surface is overlay\n");
}

static void testNotAppliedMeansInactive() {
    PostEffectState s;
    s.mode = PostEffectMode::Threshold;
    s.applied = false;

    assert(!s.isActive());
    printf("  PASS: not applied means inactive\n");
}

static void testClearResetsAll() {
    PostEffectState s;
    s.mode = PostEffectMode::ClipPlane;
    s.applied = true;
    s.clipPlane = {0, 3.0f, glm::vec3(3, 0, 0), glm::vec3(1, 0, 0), false};
    s.filteredRD = makeSimpleRD(2);

    s.clear();

    assert(s.mode == PostEffectMode::None);
    assert(!s.applied);
    assert(!s.isActive());
    assert(s.filteredRD.triangleCount() == 0);
    printf("  PASS: clear resets all\n");
}

// ════════════════════════════════════════════════════════════
// 状态转换：模拟 MainWindow::beginPostEffect 逻辑
// ════════════════════════════════════════════════════════════

static void beginPostEffect(PostEffectState& post, PostEffectMode mode,
                             bool& needRestoreBaseMesh) {
    bool wasReplacement = post.isActive() && post.isMeshReplacement();
    bool willReplace = (mode == PostEffectMode::Threshold
                     || mode == PostEffectMode::ClipPlane);

    needRestoreBaseMesh = wasReplacement && !willReplace;

    post.clear();
    post.mode = mode;
}

static void testThresholdToClipPlane() {
    PostEffectState post;
    post.mode = PostEffectMode::Threshold;
    post.applied = true;
    post.filteredRD = makeSimpleRD(1);

    bool needRestore = false;
    beginPostEffect(post, PostEffectMode::ClipPlane, needRestore);

    assert(!needRestore);
    assert(post.mode == PostEffectMode::ClipPlane);
    assert(!post.applied);
    printf("  PASS: threshold -> clip plane (no restore)\n");
}

static void testClipPlaneToSlice() {
    PostEffectState post;
    post.mode = PostEffectMode::ClipPlane;
    post.applied = true;
    post.filteredRD = makeSimpleRD(2);

    bool needRestore = false;
    beginPostEffect(post, PostEffectMode::Slice, needRestore);

    assert(needRestore);
    assert(post.mode == PostEffectMode::Slice);
    printf("  PASS: clip plane -> slice (restore base mesh)\n");
}

static void testIsoSurfaceToClear() {
    PostEffectState post;
    post.mode = PostEffectMode::IsoSurface;
    post.applied = true;
    post.iso = {"Temp", 25.0f};

    bool wasReplacement = post.isActive() && post.isMeshReplacement();
    post.clear();

    assert(!wasReplacement);
    assert(!post.isActive());
    printf("  PASS: iso surface -> clear (no restore needed)\n");
}

static void testThresholdToClear() {
    PostEffectState post;
    post.mode = PostEffectMode::Threshold;
    post.applied = true;
    post.filteredRD = makeSimpleRD(3);

    bool wasReplacement = post.isActive() && post.isMeshReplacement();
    post.clear();

    assert(wasReplacement);
    assert(!post.isActive());
    printf("  PASS: threshold -> clear (needs restore)\n");
}

static void testSliceToIsoSurface() {
    PostEffectState post;
    post.mode = PostEffectMode::Slice;
    post.applied = true;

    bool needRestore = false;
    beginPostEffect(post, PostEffectMode::IsoSurface, needRestore);

    assert(!needRestore);
    assert(post.mode == PostEffectMode::IsoSurface);
    printf("  PASS: slice -> iso surface (no restore)\n");
}

// ════════════════════════════════════════════════════════════
// displayRenderData 与 contour 的组合
// ════════════════════════════════════════════════════════════

static void testDisplayRenderDataUsesFilteredWhenReplacement() {
    FERenderData baseRD = makeSimpleRD(4);
    DeformState deform;

    PostEffectState post;
    post.mode = PostEffectMode::Threshold;
    post.applied = true;
    post.filteredRD = makeSimpleRD(2);

    const FERenderData& display = displayRenderData(post, deform, baseRD);
    assert(display.triangleCount() == 2);
    printf("  PASS: displayRenderData uses filtered when replacement\n");
}

static void testDisplayRenderDataUsesBaseWhenOverlay() {
    FERenderData baseRD = makeSimpleRD(4);
    DeformState deform;

    PostEffectState post;
    post.mode = PostEffectMode::Slice;
    post.applied = true;

    const FERenderData& display = displayRenderData(post, deform, baseRD);
    assert(display.triangleCount() == 4);
    printf("  PASS: displayRenderData uses base when overlay\n");
}

static void testDisplayRenderDataUsesDeformedWhenActive() {
    FERenderData baseRD = makeSimpleRD(4);
    DeformState deform;
    deform.active = true;
    deform.renderData = makeSimpleRD(4);
    deform.renderData.triangleToElement = {200, 201, 202, 203};

    PostEffectState post;

    const FERenderData& display = displayRenderData(post, deform, baseRD);
    assert(display.triangleToElement[0] == 200);
    printf("  PASS: displayRenderData uses deformed when active\n");
}

static void testContourReapplyAfterFilter() {
    ContourState contour;
    contour.active = true;
    contour.field.name = "Stress";
    contour.field.location = FieldLocation::Node;
    contour.title = "Von Mises";

    PostEffectState post;
    post.mode = PostEffectMode::Threshold;
    post.applied = true;
    post.filteredRD = makeSimpleRD(2);

    assert(contour.active);
    assert(contour.title == "Von Mises");

    post.clear();
    assert(!post.isActive());
    assert(contour.active);
    printf("  PASS: contour survives filter clear\n");
}

static void testContourReapplyAfterFilterSwitch() {
    ContourState contour;
    contour.active = true;
    contour.field.name = "Temperature";
    contour.title = "T";

    PostEffectState post;
    post.mode = PostEffectMode::Threshold;
    post.applied = true;
    post.filteredRD = makeSimpleRD(2);

    bool needRestore = false;
    beginPostEffect(post, PostEffectMode::ClipPlane, needRestore);

    assert(contour.active);
    assert(contour.title == "T");
    printf("  PASS: contour survives filter mode switch\n");
}

// ════════════════════════════════════════════════════════════
// DeformState / ContourState 基础
// ════════════════════════════════════════════════════════════

static void testDeformStateClear() {
    DeformState d;
    d.active = true;
    d.scale = 10.0f;
    d.overlay = true;
    d.renderData = makeSimpleRD(2);

    d.clear();
    assert(!d.active);
    assert(d.scale == 1.0f);
    assert(!d.overlay);
    assert(d.renderData.triangleCount() == 0);
    printf("  PASS: deform state clear\n");
}

static void testContourStateClear() {
    ContourState c;
    c.active = true;
    c.field.name = "Stress";
    c.title = "Von Mises";

    c.clear();
    assert(!c.active);
    assert(c.title.isEmpty());
    printf("  PASS: contour state clear\n");
}

static void testActiveRenderDataWithDeform() {
    FERenderData baseRD = makeSimpleRD(4);
    DeformState deform;

    assert(&activeRenderData(deform, baseRD) == &baseRD);

    deform.active = true;
    deform.renderData = makeSimpleRD(4);
    assert(&activeRenderData(deform, baseRD) == &deform.renderData);
    printf("  PASS: activeRenderData switches on deform\n");
}

// ════════════════════════════════════════════════════════════
// 参数记录完整性
// ════════════════════════════════════════════════════════════

static void testParamsRecordedCorrectly() {
    PostEffectState post;
    post.mode = PostEffectMode::ClipPlane;
    post.applied = true;
    post.clipPlane = {1, 5.5f, glm::vec3(0, 5.5f, 0), glm::vec3(0, 1, 0), false};

    assert(post.clipPlane.axis == 1);
    assert(post.clipPlane.offset == 5.5f);
    assert(post.clipPlane.origin.y == 5.5f);
    assert(post.clipPlane.normal.y == 1.0f);
    assert(!post.clipPlane.keepPositive);

    post.clear();
    post.mode = PostEffectMode::IsoSurface;
    post.applied = true;
    post.iso = {"Temperature", 42.0f};

    assert(post.iso.fieldName == "Temperature");
    assert(post.iso.isoValue == 42.0f);
    printf("  PASS: params recorded correctly\n");
}

int main() {
    printf("=== PostEffectState Tests ===\n");
    testInitialState();
    testThresholdIsReplacement();
    testClipPlaneIsReplacement();
    testSliceIsOverlay();
    testIsoSurfaceIsOverlay();
    testNotAppliedMeansInactive();
    testClearResetsAll();

    printf("\n=== State Transitions ===\n");
    testThresholdToClipPlane();
    testClipPlaneToSlice();
    testIsoSurfaceToClear();
    testThresholdToClear();
    testSliceToIsoSurface();

    printf("\n=== Display & Contour Integration ===\n");
    testDisplayRenderDataUsesFilteredWhenReplacement();
    testDisplayRenderDataUsesBaseWhenOverlay();
    testDisplayRenderDataUsesDeformedWhenActive();
    testContourReapplyAfterFilter();
    testContourReapplyAfterFilterSwitch();

    printf("\n=== DeformState / ContourState ===\n");
    testDeformStateClear();
    testContourStateClear();
    testActiveRenderDataWithDeform();

    printf("\n=== Params ===\n");
    testParamsRecordedCorrectly();

    printf("\nAll PostState tests passed!\n");
    return 0;
}
