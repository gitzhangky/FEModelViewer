#include "Camera.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

static void requireFiniteMatrix(const glm::mat4& m)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            assert(std::isfinite(m[c][r]));
}

int main()
{
    Camera cam;
    cam.target = glm::vec3(1.0f, 2.0f, 3.0f);
    cam.distance = 5.0f;

    cam.setStandardView(StandardView::Front);
    glm::vec3 eye = cam.eye();
    assert(near(eye.x, 1.0f) && near(eye.y, 2.0f) && near(eye.z, 8.0f));

    cam.setStandardView(StandardView::Back);
    eye = cam.eye();
    assert(near(eye.x, 1.0f) && near(eye.y, 2.0f) && near(eye.z, -2.0f));

    cam.setStandardView(StandardView::Right);
    eye = cam.eye();
    assert(near(eye.x, 6.0f) && near(eye.y, 2.0f) && near(eye.z, 3.0f));

    cam.setStandardView(StandardView::Left);
    eye = cam.eye();
    assert(near(eye.x, -4.0f) && near(eye.y, 2.0f) && near(eye.z, 3.0f));

    cam.setStandardView(StandardView::Top);
    eye = cam.eye();
    assert(near(eye.x, 1.0f) && near(eye.y, 7.0f) && near(eye.z, 3.0f));
    requireFiniteMatrix(cam.viewMatrix());

    cam.setStandardView(StandardView::Bottom);
    eye = cam.eye();
    assert(near(eye.x, 1.0f) && near(eye.y, -3.0f) && near(eye.z, 3.0f));
    requireFiniteMatrix(cam.viewMatrix());

    std::printf("PASS: camera standard views\n");
    return 0;
}
