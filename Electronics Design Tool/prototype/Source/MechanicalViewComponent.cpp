#include "MechanicalViewComponent.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace mechanical_scene
{

MechanicalViewComponent::MechanicalViewComponent()
    : MechanicalViewComponent(nullptr)
{
}

MechanicalViewComponent::MechanicalViewComponent(std::shared_ptr<sim_panel::SystemSimulationSession> session)
    : session_(session)
{
    setOpaque(true);

    // Initial default camera setup
    camera_.yaw = -40.0;
    camera_.pitch = 22.0;
    camera_.zoom = 0.9;
    camera_.panX = 0.0;
    camera_.panY = 0.0;
    camera_.perspective = true;

    // Load default dual-motor scene
    scene_ = MechanicalScene::createDualMotorShaftScene();

    // Setup controls
    titleLabel.setText("Mechanical View", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(titleLabel);

    sceneSelector.addItem("Dual Motor Assembly", 1);
    sceneSelector.addItem("Controlled Robotic Arm", 2);
    sceneSelector.setSelectedId(1, juce::dontSendNotification);
    sceneSelector.onChange = [this] {
        if (sceneSelector.getSelectedId() == 1)
            setScene(MechanicalScene::createDualMotorShaftScene());
        else if (sceneSelector.getSelectedId() == 2)
            setScene(MechanicalScene::createControlledArmScene());
    };
    addAndMakeVisible(sceneSelector);

    resetViewButton.onClick = [this] { resetCamera(); };
    addAndMakeVisible(resetViewButton);

    homePoseButton.onClick = [this] {
        scene_.resetToRestPose();
        repaint();
    };
    addAndMakeVisible(homePoseButton);

    telemetryLabel.setFont(juce::Font(12.0f));
    telemetryLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fb3bc));
    addAndMakeVisible(telemetryLabel);

    // Start 60 Hz visualization timer
    startTimerHz(60);
}

MechanicalViewComponent::~MechanicalViewComponent()
{
    stopTimer();
}

void MechanicalViewComponent::setScene(const MechanicalScene& scene)
{
    scene_ = scene;
    scene_.evaluateTransforms();
    repaint();
}

void MechanicalViewComponent::resetCamera()
{
    camera_.yaw = -40.0;
    camera_.pitch = 22.0;
    camera_.zoom = 0.9;
    camera_.panX = 0.0;
    camera_.panY = 0.0;
    repaint();
}

void MechanicalViewComponent::selectBody(const std::string& bodyId)
{
    selectedBodyId_ = bodyId;
    repaint();
}

void MechanicalViewComponent::resized()
{
    auto area = getLocalBounds();
    auto topStrip = area.removeFromTop(32).reduced(4);

    titleLabel.setBounds(topStrip.removeFromLeft(130));
    sceneSelector.setBounds(topStrip.removeFromLeft(180));
    topStrip.removeFromLeft(8);
    resetViewButton.setBounds(topStrip.removeFromLeft(90));
    topStrip.removeFromLeft(6);
    homePoseButton.setBounds(topStrip.removeFromLeft(90));
    topStrip.removeFromLeft(12);
    telemetryLabel.setBounds(topStrip);
}

void MechanicalViewComponent::timerCallback()
{
    if (session_ != nullptr)
    {
        const auto snap = session_->getSnapshot();
        const double dt = (snap.simTime >= lastSimTime_) ? (snap.simTime - lastSimTime_) : 0.0;
        lastSimTime_ = snap.simTime;

        // Drive mechanical scene animation directly from authoritative simulation snapshot
        scene_.updateFromSimulation(snap, dt);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3)
           << "Sim: " << snap.simTime << " s | Rate: " << snap.targetRate << "x";

        if (!selectedJointId_.empty())
        {
            if (const auto* j = scene_.findJoint(selectedJointId_))
            {
                ss << " | " << j->name << ": " << std::setprecision(2) << j->currentValue << " rad ("
                   << j->currentVelocity << " rad/s)";
            }
        }
        else if (scene_.joints.size() > 0)
        {
            const auto& j0 = scene_.joints[0];
            ss << " | " << j0.name << ": " << std::setprecision(2) << j0.currentValue << " rad";
            if (scene_.joints.size() > 1)
            {
                const auto& j1 = scene_.joints[1];
                ss << " | " << j1.name << ": " << std::setprecision(2) << j1.currentValue << " rad";
            }
        }
        telemetryLabel.setText(ss.str(), juce::dontSendNotification);
    }
    repaint();
}

void MechanicalViewComponent::mouseDown(const juce::MouseEvent& e)
{
    lastMousePos_ = e.getPosition();
    isDragging_ = true;
}

void MechanicalViewComponent::mouseDrag(const juce::MouseEvent& e)
{
    const auto delta = e.getPosition() - lastMousePos_;
    lastMousePos_ = e.getPosition();

    if (e.mods.isRightButtonDown() || e.mods.isShiftDown())
    {
        // Pan
        camera_.panX += delta.x * 0.003;
        camera_.panY -= delta.y * 0.003;
    }
    else
    {
        // Orbit
        camera_.yaw += delta.x * 0.5;
        camera_.pitch = std::clamp(camera_.pitch - delta.y * 0.5, -89.0, 89.0);
    }
    repaint();
}

void MechanicalViewComponent::mouseUp(const juce::MouseEvent&)
{
    isDragging_ = false;
}

void MechanicalViewComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const double factor = (wheel.deltaY > 0) ? 1.1 : 0.9;
    camera_.zoom = std::clamp(camera_.zoom * factor, 0.2, 5.0);
    repaint();
}

void MechanicalViewComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d131a));

    auto viewArea = getLocalBounds().toFloat();
    viewArea.removeFromTop(32.0f); // Top toolbar

    renderScene3D(g, viewArea);
    renderHud(g, viewArea);
}

void MechanicalViewComponent::renderScene3D(juce::Graphics& g, juce::Rectangle<float> area)
{
    // Render Ground Grid
    g.setColour(juce::Colour(0xff18222c));
    const int gridLines = 10;
    const double gridSpacing = 0.2;
    for (int i = -gridLines; i <= gridLines; ++i)
    {
        double pos = i * gridSpacing;
        auto p1 = plot_instrument::project(camera_, pos, -gridLines * gridSpacing, -0.1, area);
        auto p2 = plot_instrument::project(camera_, pos, gridLines * gridSpacing, -0.1, area);
        g.drawLine(p1.x, p1.y, p2.x, p2.y, 1.0f);

        auto p3 = plot_instrument::project(camera_, -gridLines * gridSpacing, pos, -0.1, area);
        auto p4 = plot_instrument::project(camera_, gridLines * gridSpacing, pos, -0.1, area);
        g.drawLine(p3.x, p3.y, p4.x, p4.y, 1.0f);
    }

    // Sort bodies by camera depth for basic painter's algorithm
    struct DepthBody
    {
        const RigidBody* body;
        double depth;
    };
    std::vector<DepthBody> sortedBodies;
    for (const auto& b : scene_.bodies)
    {
        Vec3 center = b.worldTransform.transformPoint({ 0, 0, 0 });
        auto p = plot_instrument::project(camera_, center.x, center.y, center.z, area);
        sortedBodies.push_back({ &b, p.depth });
    }
    std::sort(sortedBodies.begin(), sortedBodies.end(), [](const DepthBody& a, const DepthBody& b) {
        return a.depth > b.depth; // draw furthest first
    });

    for (const auto& item : sortedBodies)
    {
        renderBodyGeometry(g, *item.body, area);
    }

    // Render Joint Overlays
    for (const auto& j : scene_.joints)
    {
        renderJointOverlay(g, j, area);
    }
}

void MechanicalViewComponent::renderBodyGeometry(juce::Graphics& g, const RigidBody& body, juce::Rectangle<float> area)
{
    const bool isSelected = (body.id == selectedBodyId_);

    for (const auto& geo : body.geometries)
    {
        const Mat4 localGeo = body.worldTransform * Mat4::translate(geo.offset);

        if (geo.type == PrimitiveType::Box)
        {
            // Compute 8 corners of the box
            const double hx = geo.size.x * 0.5;
            const double hy = geo.size.y * 0.5;
            const double hz = geo.size.z * 0.5;

            Vec3 corners[8] = {
                localGeo.transformPoint({ -hx, -hy, -hz }),
                localGeo.transformPoint({  hx, -hy, -hz }),
                localGeo.transformPoint({  hx,  hy, -hz }),
                localGeo.transformPoint({ -hx,  hy, -hz }),
                localGeo.transformPoint({ -hx, -hy,  hz }),
                localGeo.transformPoint({  hx, -hy,  hz }),
                localGeo.transformPoint({  hx,  hy,  hz }),
                localGeo.transformPoint({ -hx,  hy,  hz })
            };

            juce::Point<float> pts[8];
            for (int i = 0; i < 8; ++i)
            {
                auto proj = plot_instrument::project(camera_, corners[i].x, corners[i].y, corners[i].z, area);
                pts[i] = { proj.x, proj.y };
            }

            // Draw shaded faces
            auto drawQuad = [&](int i0, int i1, int i2, int i3, juce::Colour col) {
                juce::Path p;
                p.startNewSubPath(pts[i0]);
                p.lineTo(pts[i1]);
                p.lineTo(pts[i2]);
                p.lineTo(pts[i3]);
                p.closeSubPath();
                g.setColour(col);
                g.fillPath(p);
                g.setColour(col.brighter(0.2f));
                g.strokePath(p, juce::PathStrokeType(1.2f));
            };

            juce::Colour c = isSelected ? juce::Colour(0xffffe066) : geo.colour;
            drawQuad(0, 1, 2, 3, c.darker(0.3f)); // back
            drawQuad(4, 5, 6, 7, c.brighter(0.1f)); // front
            drawQuad(0, 4, 7, 3, c.darker(0.1f)); // left
            drawQuad(1, 5, 6, 2, c);              // right
            drawQuad(3, 2, 6, 7, c.brighter(0.2f)); // top
            drawQuad(0, 1, 5, 4, c.darker(0.4f)); // bottom
        }
        else if (geo.type == PrimitiveType::Cylinder || geo.type == PrimitiveType::Wheel)
        {
            // Render faceted cylinder / disc
            const int slices = 16;
            const double r = geo.size.x * 0.5;
            const double len = geo.size.y;
            const double hLen = len * 0.5;

            std::vector<juce::Point<float>> topPts(slices);
            std::vector<juce::Point<float>> botPts(slices);

            for (int i = 0; i < slices; ++i)
            {
                double angle = (2.0 * juce::MathConstants<double>::pi * i) / slices;
                double px = r * std::cos(angle);
                double pz = r * std::sin(angle);

                Vec3 ptTop = localGeo.transformPoint({ px, hLen, pz });
                Vec3 ptBot = localGeo.transformPoint({ px, -hLen, pz });

                auto prTop = plot_instrument::project(camera_, ptTop.x, ptTop.y, ptTop.z, area);
                auto prBot = plot_instrument::project(camera_, ptBot.x, ptBot.y, ptBot.z, area);

                topPts[i] = { prTop.x, prTop.y };
                botPts[i] = { prBot.x, prBot.y };
            }

            juce::Colour c = isSelected ? juce::Colour(0xffffe066) : geo.colour;

            // Draw body side quads
            for (int i = 0; i < slices; ++i)
            {
                int next = (i + 1) % slices;
                juce::Path p;
                p.startNewSubPath(topPts[i]);
                p.lineTo(topPts[next]);
                p.lineTo(botPts[next]);
                p.lineTo(botPts[i]);
                p.closeSubPath();
                float shade = 0.8f + 0.3f * std::cos((2.0 * juce::MathConstants<double>::pi * i) / slices);
                g.setColour(c.withMultipliedBrightness(shade));
                g.fillPath(p);
            }

            // Draw top cap
            juce::Path topPath;
            topPath.startNewSubPath(topPts[0]);
            for (int i = 1; i < slices; ++i)
                topPath.lineTo(topPts[i]);
            topPath.closeSubPath();
            g.setColour(c.brighter(0.2f));
            g.fillPath(topPath);
            g.setColour(c.brighter(0.4f));
            g.strokePath(topPath, juce::PathStrokeType(1.2f));

            // If wheel, render rotation alignment marker (stripe)
            if (geo.type == PrimitiveType::Wheel)
            {
                Vec3 m1 = localGeo.transformPoint({ 0.0, hLen + 0.005, 0.0 });
                Vec3 m2 = localGeo.transformPoint({ r * 0.9, hLen + 0.005, 0.0 });
                auto pm1 = plot_instrument::project(camera_, m1.x, m1.y, m1.z, area);
                auto pm2 = plot_instrument::project(camera_, m2.x, m2.y, m2.z, area);
                g.setColour(geo.accentColour);
                g.drawLine(pm1.x, pm1.y, pm2.x, pm2.y, 3.0f);
            }
        }
        else if (geo.type == PrimitiveType::Pointer)
        {
            // Indicator needle
            Vec3 p0 = localGeo.transformPoint({ 0, 0, 0 });
            Vec3 p1 = localGeo.transformPoint({ 0, geo.size.y, 0 });
            auto pr0 = plot_instrument::project(camera_, p0.x, p0.y, p0.z, area);
            auto pr1 = plot_instrument::project(camera_, p1.x, p1.y, p1.z, area);
            g.setColour(geo.colour);
            g.drawLine(pr0.x, pr0.y, pr1.x, pr1.y, 3.5f);
        }
    }
}

void MechanicalViewComponent::renderJointOverlay(juce::Graphics& g, const Joint& joint, juce::Rectangle<float> area)
{
    // Find parent body to resolve joint anchor in world space
    for (const auto& b : scene_.bodies)
    {
        if (b.jointId == joint.id)
        {
            Vec3 anchorWorld = b.worldTransform.transformPoint({ 0, 0, 0 });
            auto proj = plot_instrument::project(camera_, anchorWorld.x, anchorWorld.y, anchorWorld.z, area);

            // Draw small glowing joint origin
            g.setColour(juce::Colour(0xff4cc9f0).withAlpha(0.7f));
            g.drawEllipse(proj.x - 4.0f, proj.y - 4.0f, 8.0f, 8.0f, 1.5f);
            break;
        }
    }
}

void MechanicalViewComponent::renderHud(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour(0xff8899a6));
    g.setFont(11.0f);
    g.drawText("Left-drag: Orbit | Right-drag / Shift-drag: Pan | Wheel: Zoom",
               area.removeFromBottom(24.0f).reduced(8.0f, 0.0f),
               juce::Justification::bottomLeft);
}

} // namespace mechanical_scene
