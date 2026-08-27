#pragma once
#include <glm/glm.hpp>
#include <juce_core/juce_core.h>

namespace Harmonia {
class IRegion {
public:
    virtual ~IRegion() = default;
    virtual void enter() = 0;
    virtual void exit() = 0;
    virtual void update(float dt) = 0;
    virtual void render(const glm::mat4& view, const glm::mat4& proj) = 0;
    virtual juce::String name() const = 0;
    virtual glm::vec3 worldCenter() const = 0;
    virtual float radius() const = 0;
};
}
