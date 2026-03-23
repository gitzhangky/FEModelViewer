/**
 * @file Camera.cpp
 * @brief 轨道相机实现
 */

#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>  // glm::lookAt, glm::radians, glm::clamp
#include <cmath>                         // cos, sin

glm::vec3 Camera::eye() const {
    // 将角度转换为弧度
    float ry = glm::radians(yaw);
    float rp = glm::radians(pitch);

    // 球坐标 -> 笛卡尔坐标
    // X = cos(pitch) * sin(yaw)   → 水平平面上的左右偏移
    // Y = sin(pitch)              → 上下偏移（仰俯）
    // Z = cos(pitch) * cos(yaw)   → 水平平面上的前后偏移
    return target + distance * glm::vec3(
        cos(rp) * sin(ry),
        sin(rp),
        cos(rp) * cos(ry)
    );
}

glm::mat4 Camera::viewMatrix() const {
    // glm::lookAt(eye, center, up) 生成观察矩阵
    // eye:    相机位置
    // center: 注视目标点
    // up:     世界空间的上方向（Y 轴正方向）
    return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

void Camera::rotate(float dx, float dy) {
    // 鼠标水平移动 → 改变 yaw（左右旋转）
    yaw   -= dx * rotateSensitivity;
    // 鼠标垂直移动 → 改变 pitch（上下仰俯）
    pitch += dy * rotateSensitivity;
    // 限制 pitch 在 [-89°, 89°]，防止翻转（万向锁）
    pitch  = glm::clamp(pitch, -89.0f, 89.0f);
}

void Camera::pan(float dx, float dy) {
    // 从视线方向和世界 up 推导出相机坐标系的真实 right 和 up 向量
    // 这样无论相机俯仰多少度，平移方向都与屏幕像素方向完全对齐
    glm::vec3 viewDir = glm::normalize(target - eye());
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right  = glm::normalize(glm::cross(viewDir, worldUp));
    glm::vec3 up     = glm::cross(right, viewDir);

    float speed = distance * panSensitivity;

    target -= right * dx * speed;
    target += up    * dy * speed;
}

void Camera::zoom(float delta) {
    // 按比例缩放：每次滚轮缩放 10%，适应任意尺寸模型
    float factor = 1.0f - delta * 0.1f;
    distance *= factor;
    // 限制距离在合理范围内
    distance  = glm::clamp(distance, minDist, maxDist);
}
