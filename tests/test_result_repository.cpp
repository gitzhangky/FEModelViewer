#include "FEResultRepository.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

// ── 空仓库安全性 ──

void emptyRepositoryIsEmpty() {
    FEResultRepository repo;
    assert(repo.empty());
    assert(repo.frameCount() == 0);
    assert(repo.frame(0) == nullptr);
    assert(repo.frame(-1) == nullptr);
    assert(repo.frame(999) == nullptr);
    assert(repo.resultTypeNames(0).empty());
    assert(repo.resultTypeNames(-1).empty());
}

// ── 单帧基本操作 ──

void singleFrameAddAndRetrieve() {
    FEResultRepository repo;

    FEResultFrame frame;
    frame.info.subcaseId = 1;
    frame.info.stepIndex = 0;
    frame.info.frameIndex = 0;
    frame.info.value = 0.0;
    frame.info.valueLabel = "Static Load Case 1";
    frame.info.domain = FEResultDomain::Static;

    FEResultType dispType;
    dispType.name = "Displacement";
    dispType.hasVector = false;
    frame.resultTypes.push_back(dispType);

    FEResultType stressType;
    stressType.name = "Stress";
    stressType.hasVector = false;
    frame.resultTypes.push_back(stressType);

    repo.addFrame(frame);

    assert(!repo.empty());
    assert(repo.frameCount() == 1);

    const FEResultFrame* f = repo.frame(0);
    assert(f != nullptr);
    assert(f->info.subcaseId == 1);
    assert(f->info.domain == FEResultDomain::Static);
    assert(f->info.valueLabel == "Static Load Case 1");
    assert(f->resultTypes.size() == 2);
    assert(f->resultTypes[0].name == "Displacement");
    assert(f->resultTypes[1].name == "Stress");

    auto names = repo.resultTypeNames(0);
    assert(names.size() == 2);
    assert(names[0] == "Displacement");
    assert(names[1] == "Stress");

    // 越界查询仍安全
    assert(repo.frame(1) == nullptr);
    assert(repo.resultTypeNames(1).empty());
}

// ── 多帧 ──

void multipleFrames() {
    FEResultRepository repo;

    for (int i = 0; i < 5; ++i) {
        FEResultFrame frame;
        frame.info.subcaseId = 1;
        frame.info.stepIndex = 0;
        frame.info.frameIndex = i;
        frame.info.value = i * 0.1;
        frame.info.valueLabel = "t=" + std::to_string(i * 0.1) + "s";
        frame.info.domain = FEResultDomain::Time;

        FEResultType rt;
        rt.name = "Displacement";
        frame.resultTypes.push_back(rt);
        repo.addFrame(frame);
    }

    assert(repo.frameCount() == 5);

    const FEResultFrame* f0 = repo.frame(0);
    assert(f0 != nullptr);
    assert(f0->info.frameIndex == 0);
    assert(std::fabs(f0->info.value - 0.0) < 1e-9);

    const FEResultFrame* f4 = repo.frame(4);
    assert(f4 != nullptr);
    assert(f4->info.frameIndex == 4);
    assert(std::fabs(f4->info.value - 0.4) < 1e-9);
}

// ── 模态帧 ──

void modalFrames() {
    FEResultRepository repo;

    FEResultFrame frame;
    frame.info.subcaseId = 100;
    frame.info.stepIndex = 0;
    frame.info.frameIndex = 0;
    frame.info.value = 12.5;
    frame.info.valueLabel = "Mode 1 (12.5 Hz)";
    frame.info.domain = FEResultDomain::Mode;

    FEResultType rt;
    rt.name = "Displacement";
    frame.resultTypes.push_back(rt);
    repo.addFrame(frame);

    const FEResultFrame* f = repo.frame(0);
    assert(f != nullptr);
    assert(f->info.domain == FEResultDomain::Mode);
    assert(std::fabs(f->info.value - 12.5) < 1e-9);
}

// ── clear ──

void clearResetsEverything() {
    FEResultRepository repo;

    FEResultFrame frame;
    frame.info.subcaseId = 1;
    repo.addFrame(frame);
    assert(repo.frameCount() == 1);

    repo.clear();
    assert(repo.empty());
    assert(repo.frameCount() == 0);
    assert(repo.frame(0) == nullptr);
}

// ── fromResultData 转换 ──

void fromResultDataConvertsSubcases() {
    FEResultData data;

    FESubcase sc1;
    sc1.id = 1;
    sc1.name = "Subcase 1";
    FEResultType rt1;
    rt1.name = "Displacement";
    rt1.hasVector = true;
    FEResultComponent comp;
    comp.name = "Magnitude";
    comp.field.name = "Disp Mag";
    comp.field.location = FieldLocation::Node;
    comp.field.values[10] = 1.5f;
    rt1.components.push_back(comp);
    sc1.resultTypes.push_back(rt1);
    data.subcases.push_back(sc1);

    FESubcase sc2;
    sc2.id = 2;
    sc2.name = "Subcase 2";
    FEResultType rt2;
    rt2.name = "Stress";
    rt2.hasVector = false;
    sc2.resultTypes.push_back(rt2);
    data.subcases.push_back(sc2);

    FEResultRepository repo = FEResultRepository::fromResultData(data);

    assert(repo.frameCount() == 2);

    const FEResultFrame* f0 = repo.frame(0);
    assert(f0 != nullptr);
    assert(f0->info.subcaseId == 1);
    assert(f0->info.domain == FEResultDomain::Static);
    assert(f0->info.valueLabel == "Subcase 1");
    assert(f0->resultTypes.size() == 1);
    assert(f0->resultTypes[0].name == "Displacement");
    assert(f0->resultTypes[0].components.size() == 1);
    assert(std::fabs(f0->resultTypes[0].components[0].field.values.at(10) - 1.5f) < 1e-5f);

    const FEResultFrame* f1 = repo.frame(1);
    assert(f1 != nullptr);
    assert(f1->info.subcaseId == 2);
    assert(f1->info.valueLabel == "Subcase 2");
    assert(f1->resultTypes[0].name == "Stress");
}

void fromEmptyResultData() {
    FEResultData data;
    FEResultRepository repo = FEResultRepository::fromResultData(data);
    assert(repo.empty());
    assert(repo.frameCount() == 0);
}

}  // namespace

int main() {
    emptyRepositoryIsEmpty();
    singleFrameAddAndRetrieve();
    multipleFrames();
    modalFrames();
    clearResetsEverything();
    fromResultDataConvertsSubcases();
    fromEmptyResultData();
    std::cout << "test_result_repository passed\n";
    return 0;
}
