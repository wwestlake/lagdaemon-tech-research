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

    if (firstPerson_) {
        targetAzimuth_   -= delta.x * sensitivity;
        targetElevation_ += delta.y * sensitivity;
        targetElevation_  = juce::jlimit(-1.4f, 1.4f, targetElevation_);
    } else if (e.mods.isRightButtonDown()) {
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
    if (firstPerson_) return;

    targetDistance_ *= (1.0f - w.deltaY * 0.15f);
    targetDistance_  = juce::jlimit(2.f, 200.f, targetDistance_);
    animating_ = false;
}

void Camera::update(float dt) {
    if (firstPerson_) {
        const float speed = 18.f;
        const float alpha = 1.f - std::exp(-speed * dt);
        azimuth += (targetAzimuth_ - azimuth) * alpha;
        elevation += (targetElevation_ - elevation) * alpha;
        return;
    }

    if (animating_) {
        animT_ = juce::jmin(animT_ + dt / animDur_, 1.f);
        float t = animT_ * animT_ * (3.f - 2.f * animT_); // smoothstep
        azimuth   = targetAzimuth_;
        elevation = targetElevation_;
        distance  = animStartDist_ + (animEndDist_ - animStartDist_) * t;
        pivot     = animStartPos_ + (animEndTarget_ - animStartPos_) * t;
        if (animT_ >= 1.f) animating_ = false;
    }

    // Azimuth/elevation now come from continuous mouse-look
    // (applyLookDelta) most of the time, not slow click-drag orbiting -
    // damp those two quickly (matches firstPerson's own responsiveness)
    // so aim doesn't feel laggy. Distance/pivot stay on the slower damp;
    // those are camera-catching-up-to-player-position concerns, not aim.
    const float lookSpeed = 18.f;
    float lookAlpha = 1.f - std::exp(-lookSpeed * dt);
    azimuth   += (targetAzimuth_   - azimuth)   * lookAlpha;
    elevation += (targetElevation_ - elevation) * lookAlpha;

    const float speed = 10.f;
    float alpha = 1.f - std::exp(-speed * dt);
    distance  += (targetDistance_  - distance)  * alpha;
    pivot     += (targetPivot_     - pivot)     * alpha;
}

glm::vec3 Camera::position() const {
    if (firstPerson_) return firstPersonPosition_;

    float cosEl = std::cos(elevation);
    return pivot + glm::vec3(
        std::cos(azimuth) * cosEl,
        std::sin(elevation),
        std::sin(azimuth) * cosEl) * distance;
}

glm::vec3 Camera::forward() const {
    return glm::normalize(glm::vec3(
        -std::sin(azimuth) * std::cos(elevation),
         std::sin(elevation),
         std::cos(azimuth) * std::cos(elevation)));
}

glm::mat4 Camera::viewMatrix() const {
    if (firstPerson_)
        return glm::lookAt(firstPersonPosition_, firstPersonPosition_ + forward(), glm::vec3(0, 1, 0));

    return glm::lookAt(position(), pivot, glm::vec3(0, 1, 0));
}

void Camera::applyLookDelta(float dx, float dy) {
    const float sensitivity = 0.005f;
    targetAzimuth_   -= dx * sensitivity;
    targetElevation_ += dy * sensitivity;
    targetElevation_  = juce::jlimit(-1.4f, 1.4f, targetElevation_);
    animating_ = false;
}

void Camera::setFirstPersonPosition(const glm::vec3& p) {
    firstPerson_ = true;
    firstPersonPosition_ = p;
    animating_ = false;
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
    firstPerson_ = false;
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
