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
    void setFirstPersonPosition(const glm::vec3& p);
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
