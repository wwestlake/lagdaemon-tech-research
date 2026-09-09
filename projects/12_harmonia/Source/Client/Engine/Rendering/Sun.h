#pragma once

#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <memory>

namespace Harmonia {

// A visible sun disc that arcs across the sky each "day" and, over a much
// longer "season"/year cycle, rotates smoothly through the circle-of-
// fifths' 12-colour wheel (CircleOfFifths::colourForPitchClass) - the
// same colour logic already used for note/chord visualization elsewhere,
// not a separate palette invented just for this.
class Sun {
public:
    Sun();
    ~Sun();

    void update(float dt);
    void render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx);

    // World-space direction TOWARD the sun (normalized) - feeds ground/
    // world lighting so shading matches where the sun actually is, rather
    // than a fixed hardcoded light vector.
    glm::vec3 direction() const;

    // The same single-axis rotation direction() derives the sun's
    // position from - shared with StarField so the whole sky (sun AND
    // stars) turns together as one coherent rotating dome, not a moving
    // sun over a frozen star field.
    glm::mat4 skyRotation() const;

    // Current sun colour, smoothly blended between two adjacent wheel
    // colours as the season progresses - continuous, not a hard jump.
    glm::vec3 color() const;

private:
    void init(juce::OpenGLContext& ctx);

    float dayPhase_ = 0.5f;    // 0..1 around one day's arc - starts near midday/overhead
    float seasonPhase_ = 0.0f; // 0..1 around one full 12-colour year

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
};

}
