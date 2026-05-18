#include "LabelLayout.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void testSameBinLabelsStackInsteadOfSuppressing()
{
    std::unordered_map<long long, int> bins;

    int first = LabelLayout::nextStackOffset(100.0, 100.0, 16, 12, bins);
    int second = LabelLayout::nextStackOffset(105.0, 104.0, 16, 12, bins);
    int third = LabelLayout::nextStackOffset(111.0, 108.0, 16, 12, bins);

    require(first == 0, "first label in a bin has no offset");
    require(second > first, "second label in same bin is shifted");
    require(third > second, "third label in same bin is shifted further");
    std::printf("  PASS: same-bin labels stack instead of suppressing\n");
}

static void testDifferentBinsDoNotStack()
{
    std::unordered_map<long long, int> bins;

    int first = LabelLayout::nextStackOffset(100.0, 100.0, 16, 12, bins);
    int second = LabelLayout::nextStackOffset(160.0, 100.0, 16, 12, bins);

    require(first == 0, "first label has no offset");
    require(second == 0, "different bin label has no offset");
    std::printf("  PASS: different bins do not stack\n");
}

static void testStablePriorityKeepsPreviousVisibleLabelsFirst()
{
    std::vector<int> current = {10, 20, 30};
    std::vector<int> previous = {30, 10};

    auto ordered = LabelLayout::stablePriorityOrder(current, previous);

    require(ordered.size() == 3, "all current ids are returned");
    require(ordered[0] == 30, "previous visible label keeps first priority");
    require(ordered[1] == 10, "previous visible label keeps second priority");
    require(ordered[2] == 20, "new label is appended after previous labels");
    std::printf("  PASS: stable priority keeps previous visible labels first\n");
}

int main()
{
    std::printf("=== LabelLayout Tests ===\n");
    testSameBinLabelsStackInsteadOfSuppressing();
    testDifferentBinsDoNotStack();
    testStablePriorityKeepsPreviousVisibleLabelsFirst();
    std::printf("All LabelLayout tests passed!\n");
    return 0;
}
