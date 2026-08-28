#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Harmonia {

Camera::Camera()
    : targetAzimuth_(azimuth), targetElevation_(elevation),
      targetDistance_(distance), targetPivot_(pivot)
{
}

void Camera::mouseDown(const juce::MouseEvent& e) {
    lastMousePos_ = e.position;
}

void Camera::mouseDrag(const juce::MouseEvent& e) {
    juce::Point<float> delta = e.position - lastMousePos_;
    lastMousePos_ = e.position;

    const float sensitivity = 0.005f;

    if (e.mods.isRightButtonDown()) {
        // Right drag: pan pivot
        glm::vec3 right = glm::normalize(glm::cross(
            glm::vec3(0, 1, 0),
            glm::normalize(position() - pivot)));
        glm::vec3 up = glm::vec3(0, 1, 0);
        targetPivot_ -= right * delta.x * distance * 0.002f;
        targetPivot_ += up    * delta.y * distance * 0.002f;
    } else {
        // Left drag: orbit
        targetAzimuth_   -= delta.x * sensitivity;
        targetElevation_ += delta.y * sensitivity;
        targetElevation_  = juce::jlimit(-1.4f, 1.4f, targetElevation_);
    }
    animating_ = false;
}

void Camera::mouseWheelMove(const juce::MouseWheelDetails& w) {
    targetDistance_ *= (1.0f - w.deltaY * 0.15f);
    targetDistance_  = juce::jlimit(2.f, 200.f, targetDistance_);
    animating_ = false;
}

void Camera::update(float dt) {
    if (animating_) {
        animT_ = juce::jmin(animT_ + dt / animDur_, 1.f);
        float t = animT_ * animT_ * (3.f - 2.f * animT_); // smoothstep
        azimuth   = targetAzimuth_;
        elevation = targetElevation_;
        distance  = animStartDist_ + (animEndDist_ - animStartDist_) * t;
        pivot     = animStartPos_ + (animEndTarget_ - animStartPos_) * t;
        if (animT_ >= 1.f) animating_ = false;
    }

    // Smooth damp toward targets
    const float speed = 10.f;
    float alpha = 1.f - std::exp(-speed * dt);
    azimuth   += (targetAzimuth_   - azimuth)   * alpha;
    elevation += (targetElevation_ - elevation) * alpha;
    distance  += (targetDistance_  - distance)  * alpha;
    pivot     += (targetPivot_     - pivot)     * alpha;
}

glm::vec3 Camera::position() const {
    float cosEl = std::cos(elevation);
    return pivot + glm::vec3(
        std::cos(azimuth) * cosEl,
        std::sin(elevation),
        std::sin(azimuth) * cosEl) * distance;
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position(), pivot, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(60.f), aspectRatio, 0.1f, 2000.f);
}

void Camera::setOrientation(float az, float el, float dist) {
    azimuth = targetAzimuth_ = az;
    elevation = targetElevation_ = el;
    distance = targetDistance_ = dist;
    animating_ = false;
}

void Camera::flyTo(glm::vec3 target, float distFromTarget, float durationSec) {
    animStartPos_  = pivot;
    animEndTarget_ = target;
    animStartDist_ = distance;
    animEndDist_   = distFromTarget;
    targetPivot_   = target;
    targetDistance_= distFromTarget;
    animDur_       = durationSec;
    animT_         = 0.f;
    animating_     = true;
}

} // namespace Harmonia
