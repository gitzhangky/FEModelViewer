#include "ImportPathState.h"

#include <cassert>
#include <cstdio>

static void testOp2ModelAutoFillsResultPath()
{
    ImportPathState state;

    state.selectModelFile("/tmp/model.op2");

    assert(state.modelPath == "/tmp/model.op2");
    assert(state.resultPath == "/tmp/model.op2");
    assert(state.resultPathAutoFilled);
    printf("  PASS: op2 model auto-fills result path\n");
}

static void testNonOp2ModelClearsAutoFilledResultPath()
{
    ImportPathState state;
    state.selectModelFile("/tmp/old.op2");

    state.selectModelFile("/tmp/new.bdf");

    assert(state.modelPath == "/tmp/new.bdf");
    assert(state.resultPath.isEmpty());
    assert(!state.resultPathAutoFilled);
    printf("  PASS: non-op2 model clears auto-filled result path\n");
}

static void testManualResultPathSurvivesModelChange()
{
    ImportPathState state;
    state.selectResultFile("/tmp/results.op2");

    state.selectModelFile("/tmp/model.bdf");

    assert(state.modelPath == "/tmp/model.bdf");
    assert(state.resultPath == "/tmp/results.op2");
    assert(!state.resultPathAutoFilled);
    printf("  PASS: manual result path survives model change\n");
}

static void testSameOp2PathIsInferredAsAutoFilledOnRestore()
{
    ImportPathState state;

    state.restore("/tmp/model.op2", "/tmp/model.op2", true);

    assert(state.modelPath == "/tmp/model.op2");
    assert(state.resultPath == "/tmp/model.op2");
    assert(state.resultPathAutoFilled);
    printf("  PASS: same op2 path can restore auto-filled state\n");
}

static void testExplicitManualRestoreWinsOverInference()
{
    ImportPathState state;

    state.restore("/tmp/model.op2", "/tmp/model.op2", false);

    assert(state.modelPath == "/tmp/model.op2");
    assert(state.resultPath == "/tmp/model.op2");
    assert(!state.resultPathAutoFilled);
    printf("  PASS: explicit manual restore wins over inference\n");
}

int main()
{
    printf("=== ImportPathState Tests ===\n");
    testOp2ModelAutoFillsResultPath();
    testNonOp2ModelClearsAutoFilledResultPath();
    testManualResultPathSurvivesModelChange();
    testSameOp2PathIsInferredAsAutoFilledOnRestore();
    testExplicitManualRestoreWinsOverInference();
    printf("All ImportPathState tests passed!\n");
    return 0;
}
