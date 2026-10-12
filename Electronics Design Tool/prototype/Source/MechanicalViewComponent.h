#pragma once

#include "MechanicalSceneModel.h"
#include "PlotInstrument.h"
#include "SystemSimulationSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace mechanical_scene
{

class MechanicalViewComponent final : public juce::Component, private juce::Timer
{
public:
    MechanicalViewComponent();
    explicit MechanicalViewComponent(std::shared_ptr<sim_panel::SystemSimulationSession> session);
    ~MechanicalViewComponent() override;

    void setSession(std::shared_ptr<sim_panel::SystemSimulationSession> session) { session_ = session; }
    sim_panel::SystemSimulationSession* getSession() const { return session_.get(); }

    void setScene(const MechanicalScene& scene);
    const MechanicalScene& getScene() const { return scene_; }

    void resetCamera();
    void selectBody(const std::string& bodyId);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    void timerCallback() override;

    // 3D Rendering subroutines
    void renderScene3D(juce::Graphics& g, juce::Rectangle<float> area);
    void renderBodyGeometry(juce::Graphics& g, const RigidBody& body, juce::Rectangle<float> area);
    void renderJointOverlay(juce::Graphics& g, const Joint& joint, juce::Rectangle<float> area);
    void renderHud(juce::Graphics& g, juce::Rectangle<float> area);

    std::shared_ptr<sim_panel::SystemSimulationSession> session_;
    MechanicalScene scene_;

    // Camera view (reuses plot_instrument::Camera and project math)
    plot_instrument::Camera camera_;
    juce::Point<int> lastMousePos_;
    bool isDragging_ = false;

    // Selection
    std::string selectedBodyId_;
    std::string selectedJointId_;

    // Telemetry tracking
    double lastSimTime_ = 0.0;
    double fps_ = 60.0;

    // Top control strip
    juce::Label titleLabel;
    juce::ComboBox sceneSelector;
    juce::TextButton resetViewButton { "Reset View" };
    juce::TextButton homePoseButton { "Home Pose" };
    juce::Label telemetryLabel;
};

} // namespace mechanical_scene
