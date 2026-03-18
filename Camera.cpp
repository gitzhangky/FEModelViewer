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
    yaw   += dx * rotateSensitivity;
    // 鼠标垂直移动 → 改变 pitch（上下仰俯）
    pitch += dy * rotateSensitivity;
    // 限制 pitch 在 [-89°, 89°]，防止翻转（万向锁）
    pitch  = glm::clamp(pitch, -89.0f, 89.0f);
}

void Camera::pan(float dx, float dy) {
    float ry = glm::radians(yaw);

    // 根据当前 yaw 角度计算相机坐标系的右方向（在 XZ 平面上）
    glm::vec3 right(cos(ry), 0.0f, -sin(ry));
    // 上方向固定为世界 Y 轴
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    // 平移速度与距离成正比：距离远时平移快，距离近时平移细腻
    float speed = distance * panSensitivity;

    // 移动目标点（反向移动以匹配鼠标拖拽的直觉方向）
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
