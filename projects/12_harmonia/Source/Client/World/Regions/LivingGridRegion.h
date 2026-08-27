#pragma once
#include "IRegion.h"

namespace Harmonia {
class LivingGridRegion : public IRegion {
public:
    void enter() override;
    void exit() override;
    void update(float dt) override;
    void render(const glm::mat4& view, const glm::mat4& proj) override;
    juce::String name() const override { return "Living Grid"; }
    glm::vec3 worldCenter() const override { return {0,0,0}; }
    float radius() const override { return 100.0f; }
};
}
