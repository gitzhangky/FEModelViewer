#pragma once

#include <algorithm>

namespace ZoomPolicy {

inline float dynamicMinDistance(float modelSize,
                                float focusRadius,
                                float defaultMinDistance)
{
    const float safeDefault = std::max(defaultMinDistance, 1e-4f);
    const float safeModelSize = std::max(modelSize, 0.0f);
    const float safeFocusRadius = std::max(focusRadius, 0.0f);

    const float absoluteFloor = std::max(safeModelSize * 1e-4f, 1e-4f);
    const float focusDrivenMin = safeFocusRadius * 0.2f;
    return std::clamp(focusDrivenMin, absoluteFloor, safeDefault);
}

} // namespace ZoomPolicy
