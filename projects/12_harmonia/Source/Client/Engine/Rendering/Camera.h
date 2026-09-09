#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Harmonia {
class Camera {
public:
    Camera();
    void mouseDown(const juce::MouseEvent&);
    void mouseDrag(const juce::MouseEvent&);
    void mouseWheelMove(const juce::MouseWheelDetails&);
    void update(float dt);
    
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;
    glm::vec3 position() const;
    glm::vec3 forward() const;
    void setPivot(const glm::vec3& p) { firstPerson_ = false; targetPivot_ = pivot = p; }
    // Spring-arm style follow (UE4-esque): moves only the TARGET, letting
    // Camera::update()'s existing exponential damping lag smoothly behind
    // it every frame - unlike setPivot() above, which snaps both the
    // live position and the target together instantly (no lag possible).
    void setPivotTarget(const glm::vec3& p) { firstPerson_ = false; targetPivot_ = p; }
    void setFirstPersonPosition(const glm::vec3& p);
    // Continuous FPS/industry-standard mouse-look: applies a raw mouse
    // delta directly to azimuth/elevation, regardless of first/third-
    // person mode and regardless of any mouse button being held. This is
    // the PRIMARY way azimuth/elevation change now - mouseDown/mouseDrag
    // (click-drag orbit) are a separate, not-default input path.
    void applyLookDelta(float dx, float dy);
    void flyTo(glm::vec3 target, float distanceFromTarget, float durationSec);
    void setOrientation(float az, float el, float dist);  // instant, no animation
    
    float azimuth = 0.3f;
    float elevation = 0.4f;
    float distance = 5.0f;
    glm::vec3 pivot = {0,0,0};
    
private:
    float targetAzimuth_, targetElevation_, targetDistance_;
    glm::vec3 targetPivot_;
    juce::Point<float> lastMousePos_;
    bool animating_ = false;
    float animT_ = 0.f, animDur_ = 1.f;
    glm::vec3 animStartPos_, animEndTarget_;
    float animStartDist_, animEndDist_;
    bool firstPerson_ = false;
    glm::vec3 firstPersonPosition_ = {0.f, 1.6f, 0.f};
};
}
