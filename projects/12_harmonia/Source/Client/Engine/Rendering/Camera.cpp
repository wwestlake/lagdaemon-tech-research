#include "Camera.h"
#include <algorithm>

namespace Harmonia {
Camera::Camera() : targetAzimuth_(azimuth), targetElevation_(elevation), targetDistance_(distance), targetPivot_(pivot) {}

void Camera::mouseDown(const juce::MouseEvent& e) {
    lastMousePos_ = e.position;
}

void Camera::mouseDrag(const juce::MouseEvent& e) {
    float dx = (e.position.x - lastMousePos_.x) * 0.01f;
    float dy = (e.position.y - lastMousePos_.y) * 0.01f;
    targetAzimuth_ -= dx;
    targetElevation_ -= dy;
    targetElevation_ = std::clamp(targetElevation_, -1.5f, 1.5f);
    lastMousePos_ = e.position;
}

void Camera::mouseWheelMove(const juce::MouseWheelDetails& e) {
    targetDistance_ -= e.deltaY * 10.0f;
    targetDistance_ = std::max(1.0f, targetDistance_);
}

void Camera::update(float dt) {
    if (animating_) {
        animT_ += dt;
        float t = std::min(1.0f, animT_ / animDur_);
        t = t * t * (3.0f - 2.0f * t); // smoothstep
        pivot = glm::mix(animStartPos_, animEndTarget_, t);
        distance = glm::mix(animStartDist_, animEndDist_, t);
        if (t >= 1.0f) animating_ = false;
        targetPivot_ = pivot;
        targetDistance_ = distance;
    } else {
        azimuth += (targetAzimuth_ - azimuth) * 10.0f * dt;
        elevation += (targetElevation_ - elevation) * 10.0f * dt;
        distance += (targetDistance_ - distance) * 10.0f * dt;
        pivot += (targetPivot_ - pivot) * 10.0f * dt;
    }
}

glm::mat4 Camera::viewMatrix() const {
    glm::vec3 pos = position();
    return glm::lookAt(pos, pivot, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 1000.0f);
}

glm::vec3 Camera::position() const {
    float h = distance * cos(elevation);
    float y = distance * sin(elevation);
    float x = h * sin(azimuth);
    float z = h * cos(azimuth);
    return pivot + glm::vec3(x, y, z);
}

void Camera::flyTo(glm::vec3 target, float distanceFromTarget, float durationSec) {
    animating_ = true;
    animT_ = 0.f;
    animDur_ = durationSec;
    animStartPos_ = pivot;
    animEndTarget_ = target;
    animStartDist_ = distance;
    animEndDist_ = distanceFromTarget;
}
}
