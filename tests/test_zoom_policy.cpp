#include "ZoomPolicy.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a, float b, float eps = 1e-6f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    const float modelSize = 10.0f;
    const float defaultMin = 0.5f;

    assert(near(ZoomPolicy::dynamicMinDistance(modelSize, 100.0f, defaultMin), defaultMin));
    assert(near(ZoomPolicy::dynamicMinDistance(modelSize, 1.0f, defaultMin), 0.2f));
    assert(near(ZoomPolicy::dynamicMinDistance(modelSize, 0.0f, defaultMin), 0.001f));
    assert(near(ZoomPolicy::dynamicMinDistance(0.0f, 0.0f, defaultMin), 0.0001f));

    std::printf("PASS: zoom policy dynamic min distance\n");
    return 0;
}
