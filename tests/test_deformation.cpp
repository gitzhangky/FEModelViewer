#include "FEDeformation.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

// ── 单节点位移按 scale 改变坐标 ──

void singleNodeDisplacement() {
    FEModel model;
    model.nodes[1] = {1, {0.0f, 0.0f, 0.0f}};

    FEVectorField disp;
    disp.location = FieldLocation::Node;
    disp.values[1] = {1.0f, 2.0f, 3.0f};

    FEDeformationOptions opts;
    opts.scale = 2.0f;

    FEModel result = FEDeformation::apply(model, disp, opts);
    const auto& n = result.nodes.at(1);
    assert(std::abs(n.coords.x - 2.0f) < 1e-6f);
    assert(std::abs(n.coords.y - 4.0f) < 1e-6f);
    assert(std::abs(n.coords.z - 6.0f) < 1e-6f);
}

// ── 缺失位移的节点保持原坐标 ──

void missingDisplacementKeepsOriginal() {
    FEModel model;
    model.nodes[1] = {1, {10.0f, 20.0f, 30.0f}};
    model.nodes[2] = {2, {5.0f, 5.0f, 5.0f}};

    FEVectorField disp;
    disp.location = FieldLocation::Node;
    disp.values[1] = {1.0f, 0.0f, 0.0f};
    // 节点 2 无位移

    FEDeformationOptions opts;
    opts.scale = 3.0f;

    FEModel result = FEDeformation::apply(model, disp, opts);
    // 节点 1 变形
    assert(std::abs(result.nodes.at(1).coords.x - 13.0f) < 1e-6f);
    // 节点 2 不变
    assert(std::abs(result.nodes.at(2).coords.x - 5.0f) < 1e-6f);
    assert(std::abs(result.nodes.at(2).coords.y - 5.0f) < 1e-6f);
    assert(std::abs(result.nodes.at(2).coords.z - 5.0f) < 1e-6f);
}

// ── scale=0 时模型不变 ──

void zeroScaleNoChange() {
    FEModel model;
    model.nodes[1] = {1, {1.0f, 2.0f, 3.0f}};

    FEVectorField disp;
    disp.values[1] = {100.0f, 200.0f, 300.0f};

    FEDeformationOptions opts;
    opts.scale = 0.0f;

    FEModel result = FEDeformation::apply(model, disp, opts);
    assert(std::abs(result.nodes.at(1).coords.x - 1.0f) < 1e-6f);
    assert(std::abs(result.nodes.at(1).coords.y - 2.0f) < 1e-6f);
    assert(std::abs(result.nodes.at(1).coords.z - 3.0f) < 1e-6f);
}

// ── 原始模型不被修改 ──

void originalModelUnchanged() {
    FEModel model;
    model.nodes[1] = {1, {0.0f, 0.0f, 0.0f}};

    FEVectorField disp;
    disp.values[1] = {5.0f, 5.0f, 5.0f};

    FEDeformationOptions opts;
    opts.scale = 1.0f;

    FEDeformation::apply(model, disp, opts);
    // 原始模型坐标未变
    assert(std::abs(model.nodes.at(1).coords.x) < 1e-6f);
}

// ── autoScale 计算 ──

void autoScaleComputation() {
    FEModel model;
    model.nodes[1] = {1, {0.0f, 0.0f, 0.0f}};
    model.nodes[2] = {2, {100.0f, 0.0f, 0.0f}};

    FEVectorField disp;
    disp.values[1] = {0.001f, 0.0f, 0.0f};
    disp.values[2] = {0.001f, 0.0f, 0.0f};

    // 模型尺寸约 100, 最大位移 0.001
    // autoScale(targetRatio=0.1) = (100 * 0.1) / 0.001 = 10000
    float s = FEDeformation::autoScale(model, disp, 0.1f);
    assert(std::abs(s - 10000.0f) < 1.0f);
}

// ── 零位移场 autoScale 返回 1.0 ──

void autoScaleZeroDisplacement() {
    FEModel model;
    model.nodes[1] = {1, {0.0f, 0.0f, 0.0f}};

    FEVectorField disp; // 空位移
    float s = FEDeformation::autoScale(model, disp);
    assert(std::abs(s - 1.0f) < 1e-6f);
}

// ── 多节点变形保留单元连接 ──

void elementsPreservedAfterDeformation() {
    FEModel model;
    model.nodes[1] = {1, {0.0f, 0.0f, 0.0f}};
    model.nodes[2] = {2, {1.0f, 0.0f, 0.0f}};

    FEElement elem;
    elem.id = 1;
    elem.type = ElementType::BAR2;
    elem.nodeIds = {1, 2};
    model.elements[1] = elem;

    FEVectorField disp;
    disp.values[1] = {0.0f, 1.0f, 0.0f};
    disp.values[2] = {0.0f, 1.0f, 0.0f};

    FEDeformationOptions opts;
    opts.scale = 1.0f;

    FEModel result = FEDeformation::apply(model, disp, opts);
    assert(result.elements.size() == 1);
    assert(result.elements.at(1).nodeIds.size() == 2);
    assert(result.elements.at(1).nodeIds[0] == 1);
}

} // anonymous namespace

int main() {
    singleNodeDisplacement();
    missingDisplacementKeepsOriginal();
    zeroScaleNoChange();
    originalModelUnchanged();
    autoScaleComputation();
    autoScaleZeroDisplacement();
    elementsPreservedAfterDeformation();

    std::cout << "All deformation tests passed." << std::endl;
    return 0;
}
