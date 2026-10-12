#include "MechanicalSceneModel.h"

#include <cmath>
#include <queue>
#include <set>

namespace mechanical_scene
{

// -----------------------------------------------------------------------------
// VisualGeometry JSON
// -----------------------------------------------------------------------------

juce::var VisualGeometry::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", (int)type);
    obj->setProperty("size", size.toJson());
    obj->setProperty("offset", offset.toJson());
    obj->setProperty("colour", colour.toDisplayString(true));
    obj->setProperty("accentColour", accentColour.toDisplayString(true));
    return juce::var(obj);
}

VisualGeometry VisualGeometry::fromJson(const juce::var& json)
{
    VisualGeometry g;
    if (const auto* obj = json.getDynamicObject())
    {
        g.type = (PrimitiveType)(int)obj->getProperty("type");
        g.size = Vec3::fromJson(obj->getProperty("size"), { 1.0, 1.0, 1.0 });
        g.offset = Vec3::fromJson(obj->getProperty("offset"), { 0.0, 0.0, 0.0 });
        if (obj->hasProperty("colour"))
            g.colour = juce::Colour::fromString(obj->getProperty("colour").toString());
        if (obj->hasProperty("accentColour"))
            g.accentColour = juce::Colour::fromString(obj->getProperty("accentColour").toString());
    }
    return g;
}

// -----------------------------------------------------------------------------
// SignalBinding JSON
// -----------------------------------------------------------------------------

juce::var SignalBinding::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("signalName", juce::String(signalName));
    obj->setProperty("scale", scale);
    obj->setProperty("offset", offset);
    obj->setProperty("isVelocity", isVelocity);
    if (!fallbackSignal.empty())
        obj->setProperty("fallbackSignal", juce::String(fallbackSignal));
    return juce::var(obj);
}

SignalBinding SignalBinding::fromJson(const juce::var& json)
{
    SignalBinding b;
    if (const auto* obj = json.getDynamicObject())
    {
        b.signalName = obj->getProperty("signalName").toString().toStdString();
        b.scale = obj->hasProperty("scale") ? (double)obj->getProperty("scale") : 1.0;
        b.offset = obj->hasProperty("offset") ? (double)obj->getProperty("offset") : 0.0;
        b.isVelocity = obj->hasProperty("isVelocity") ? (bool)obj->getProperty("isVelocity") : false;
        b.fallbackSignal = obj->getProperty("fallbackSignal").toString().toStdString();
    }
    return b;
}

// -----------------------------------------------------------------------------
// Joint JSON
// -----------------------------------------------------------------------------

juce::var Joint::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("name", juce::String(name));
    obj->setProperty("type", (int)type);
    obj->setProperty("axis", axis.toJson());
    obj->setProperty("anchor", anchor.toJson());

    auto* limObj = new juce::DynamicObject();
    limObj->setProperty("enabled", limits.enabled);
    limObj->setProperty("minVal", limits.minVal);
    limObj->setProperty("maxVal", limits.maxVal);
    obj->setProperty("limits", juce::var(limObj));

    obj->setProperty("binding", binding.toJson());
    return juce::var(obj);
}

Joint Joint::fromJson(const juce::var& json)
{
    Joint j;
    if (const auto* obj = json.getDynamicObject())
    {
        j.id = obj->getProperty("id").toString().toStdString();
        j.name = obj->getProperty("name").toString().toStdString();
        j.type = (JointType)(int)obj->getProperty("type");
        j.axis = Vec3::fromJson(obj->getProperty("axis"), { 0.0, 0.0, 1.0 });
        j.anchor = Vec3::fromJson(obj->getProperty("anchor"), { 0.0, 0.0, 0.0 });

        if (const auto* lObj = obj->getProperty("limits").getDynamicObject())
        {
            j.limits.enabled = lObj->hasProperty("enabled") ? (bool)lObj->getProperty("enabled") : false;
            j.limits.minVal = lObj->hasProperty("minVal") ? (double)lObj->getProperty("minVal") : -3.14159;
            j.limits.maxVal = lObj->hasProperty("maxVal") ? (double)lObj->getProperty("maxVal") : 3.14159;
        }

        j.binding = SignalBinding::fromJson(obj->getProperty("binding"));
    }
    return j;
}

// -----------------------------------------------------------------------------
// RigidBody JSON
// -----------------------------------------------------------------------------

juce::var RigidBody::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("name", juce::String(name));
    obj->setProperty("parentId", juce::String(parentId));
    obj->setProperty("jointId", juce::String(jointId));
    obj->setProperty("localPosition", localPosition.toJson());
    obj->setProperty("localRotation", localRotation.toJson());

    juce::Array<juce::var> geoArr;
    for (const auto& g : geometries)
        geoArr.add(g.toJson());
    obj->setProperty("geometries", juce::var(geoArr));

    return juce::var(obj);
}

RigidBody RigidBody::fromJson(const juce::var& json)
{
    RigidBody b;
    if (const auto* obj = json.getDynamicObject())
    {
        b.id = obj->getProperty("id").toString().toStdString();
        b.name = obj->getProperty("name").toString().toStdString();
        b.parentId = obj->getProperty("parentId").toString().toStdString();
        b.jointId = obj->getProperty("jointId").toString().toStdString();
        b.localPosition = Vec3::fromJson(obj->getProperty("localPosition"), { 0, 0, 0 });
        b.localRotation = Vec3::fromJson(obj->getProperty("localRotation"), { 0, 0, 0 });

        if (const auto* arr = obj->getProperty("geometries").getArray())
            for (const auto& item : *arr)
                b.geometries.push_back(VisualGeometry::fromJson(item));
    }
    return b;
}

// -----------------------------------------------------------------------------
// MechanicalScene Implementation
// -----------------------------------------------------------------------------

const RigidBody* MechanicalScene::findBody(const std::string& bodyId) const
{
    for (const auto& b : bodies)
        if (b.id == bodyId) return &b;
    return nullptr;
}

RigidBody* MechanicalScene::findBody(const std::string& bodyId)
{
    for (auto& b : bodies)
        if (b.id == bodyId) return &b;
    return nullptr;
}

const Joint* MechanicalScene::findJoint(const std::string& jointId) const
{
    for (const auto& j : joints)
        if (j.id == jointId) return &j;
    return nullptr;
}

Joint* MechanicalScene::findJoint(const std::string& jointId)
{
    for (auto& j : joints)
        if (j.id == jointId) return &j;
    return nullptr;
}

void MechanicalScene::resetToRestPose()
{
    for (auto& j : joints)
    {
        j.currentValue = 0.0;
        j.currentVelocity = 0.0;
    }
    evaluateTransforms();
}

void MechanicalScene::evaluateTransforms()
{
    // Build adjacency and compute worldTransform in topological order (root downwards)
    std::map<std::string, std::vector<std::string>> children;
    std::vector<std::string> roots;

    for (const auto& b : bodies)
    {
        if (b.parentId.empty())
            roots.push_back(b.id);
        else
            children[b.parentId].push_back(b.id);
    }

    std::queue<std::string> q;
    for (const auto& r : roots)
        q.push(r);

    while (!q.empty())
    {
        const auto curId = q.front();
        q.pop();

        auto* body = findBody(curId);
        if (body == nullptr)
            continue;

        Mat4 parentWorld = Mat4::identity();
        if (!body->parentId.empty())
        {
            if (const auto* p = findBody(body->parentId))
                parentWorld = p->worldTransform;
        }

        // Base local transform from resting pose
        Mat4 localT = Mat4::translate(body->localPosition);
        if (std::abs(body->localRotation.x) > 1e-6)
            localT = localT * Mat4::rotateAxis({ 1, 0, 0 }, body->localRotation.x);
        if (std::abs(body->localRotation.y) > 1e-6)
            localT = localT * Mat4::rotateAxis({ 0, 1, 0 }, body->localRotation.y);
        if (std::abs(body->localRotation.z) > 1e-6)
            localT = localT * Mat4::rotateAxis({ 0, 0, 1 }, body->localRotation.z);

        // Apply joint transform if present
        if (!body->jointId.empty())
        {
            if (const auto* j = findJoint(body->jointId))
            {
                Mat4 anchorT = Mat4::translate(j->anchor);
                Mat4 jointTransform = Mat4::identity();

                if (j->type == JointType::Revolute)
                {
                    jointTransform = Mat4::rotateAxis(j->axis, j->currentValue);
                }
                else if (j->type == JointType::Prismatic)
                {
                    jointTransform = Mat4::translate(j->axis * j->currentValue);
                }

                // Joint acts about anchor
                localT = anchorT * jointTransform * Mat4::translate(j->anchor * -1.0) * localT;
            }
        }

        body->worldTransform = parentWorld * localT;

        for (const auto& childId : children[curId])
            q.push(childId);
    }
}

void MechanicalScene::updateFromSimulation(const sim_panel::StateSnapshot& snapshot, double dtSeconds)
{
    for (auto& j : joints)
    {
        if (j.binding.signalName.empty())
            continue;

        double rawVal = 0.0;
        bool found = false;

        // Try primary signal name
        auto it = snapshot.numericSignals.find(j.binding.signalName);
        if (it != snapshot.numericSignals.end())
        {
            rawVal = it->second;
            found = true;
        }
        else if (!j.binding.fallbackSignal.empty())
        {
            it = snapshot.numericSignals.find(j.binding.fallbackSignal);
            if (it != snapshot.numericSignals.end())
            {
                rawVal = it->second;
                found = true;
            }
        }

        if (found)
        {
            if (j.binding.isVelocity)
            {
                j.currentVelocity = rawVal * j.binding.scale;
                if (dtSeconds > 0.0 && dtSeconds < 0.5)
                {
                    j.currentValue += j.currentVelocity * dtSeconds;
                }
            }
            else
            {
                j.currentValue = rawVal * j.binding.scale + j.binding.offset;

                // Also check if corresponding .speed exists for velocity display
                std::string speedSig = j.binding.signalName;
                size_t pos = speedSig.rfind(".angle");
                if (pos != std::string::npos)
                {
                    speedSig.replace(pos, 6, ".speed");
                    auto itSpd = snapshot.numericSignals.find(speedSig);
                    if (itSpd != snapshot.numericSignals.end())
                        j.currentVelocity = itSpd->second * j.binding.scale;
                }
            }

            // Enforce limits if enabled
            if (j.limits.enabled)
            {
                j.currentValue = std::clamp(j.currentValue, j.limits.minVal, j.limits.maxVal);
            }
        }
    }

    evaluateTransforms();
}

juce::var MechanicalScene::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("name", juce::String(name));
    obj->setProperty("description", juce::String(description));

    juce::Array<juce::var> bodyArr;
    for (const auto& b : bodies)
        bodyArr.add(b.toJson());
    obj->setProperty("bodies", juce::var(bodyArr));

    juce::Array<juce::var> jointArr;
    for (const auto& j : joints)
        jointArr.add(j.toJson());
    obj->setProperty("joints", juce::var(jointArr));

    return juce::var(obj);
}

MechanicalScene MechanicalScene::fromJson(const juce::var& json)
{
    MechanicalScene s;
    if (const auto* obj = json.getDynamicObject())
    {
        s.id = obj->getProperty("id").toString().toStdString();
        s.name = obj->getProperty("name").toString().toStdString();
        s.description = obj->getProperty("description").toString().toStdString();

        if (const auto* arr = obj->getProperty("bodies").getArray())
            for (const auto& item : *arr)
                s.bodies.push_back(RigidBody::fromJson(item));

        if (const auto* arr = obj->getProperty("joints").getArray())
            for (const auto& item : *arr)
                s.joints.push_back(Joint::fromJson(item));
    }
    s.evaluateTransforms();
    return s;
}

// -----------------------------------------------------------------------------
// Demonstration Scenes
// -----------------------------------------------------------------------------

// Demonstration A: Dual Motor Assembly
MechanicalScene MechanicalScene::createDualMotorShaftScene()
{
    MechanicalScene s;
    s.id = "dual_motor_scene";
    s.name = "Dual Motor Shaft Assembly";
    s.description = "Mechanical visualization of dual motor drives with independent rotating shafts, inertia flywheels, and load couplers.";

    // Base Bedplate
    RigidBody bed;
    bed.id = "bedplate";
    bed.name = "Machine Bedplate";
    VisualGeometry bedGeo;
    bedGeo.type = PrimitiveType::Box;
    bedGeo.size = { 1.8, 0.1, 0.9 };
    bedGeo.offset = { 0.0, -0.05, 0.0 };
    bedGeo.colour = juce::Colour(0xff18222b);
    bed.geometries.push_back(bedGeo);
    s.bodies.push_back(bed);

    // Motor A Housing (Fixed on left of bedplate)
    RigidBody motorA_stator;
    motorA_stator.id = "motorA_stator";
    motorA_stator.name = "Motor Drive A Stator";
    motorA_stator.parentId = "bedplate";
    motorA_stator.localPosition = { -0.5, 0.2, 0.0 };
    VisualGeometry statorAGeo;
    statorAGeo.type = PrimitiveType::Cylinder;
    statorAGeo.size = { 0.18, 0.35, 0.18 };
    statorAGeo.colour = juce::Colour(0xff2d4052);
    motorA_stator.geometries.push_back(statorAGeo);
    s.bodies.push_back(motorA_stator);

    // Motor A Rotor & Shaft (Revolute joint around Z axis)
    Joint jointA;
    jointA.id = "joint_motorA";
    jointA.name = "Motor A Rotor Joint";
    jointA.type = JointType::Revolute;
    jointA.axis = { 0.0, 0.0, 1.0 };
    jointA.anchor = { 0.0, 0.0, 0.0 };
    jointA.binding.signalName = "motorA.angle";
    jointA.binding.fallbackSignal = "motorDriveA.angle";
    jointA.binding.scale = 1.0;
    s.joints.push_back(jointA);

    RigidBody motorA_rotor;
    motorA_rotor.id = "motorA_rotor";
    motorA_rotor.name = "Motor A Shaft & Wheel";
    motorA_rotor.parentId = "motorA_stator";
    motorA_rotor.jointId = "joint_motorA";
    motorA_rotor.localPosition = { 0.0, 0.0, 0.25 };

    VisualGeometry shaftAGeo;
    shaftAGeo.type = PrimitiveType::Cylinder;
    shaftAGeo.size = { 0.04, 0.2, 0.04 };
    shaftAGeo.colour = juce::Colour(0xffb0c4de);
    motorA_rotor.geometries.push_back(shaftAGeo);

    VisualGeometry flywheelAGeo;
    flywheelAGeo.type = PrimitiveType::Wheel;
    flywheelAGeo.size = { 0.25, 0.05, 0.25 };
    flywheelAGeo.offset = { 0.0, 0.0, 0.1 };
    flywheelAGeo.colour = juce::Colour(0xff35c6ff);
    flywheelAGeo.accentColour = juce::Colour(0xffffa630);
    motorA_rotor.geometries.push_back(flywheelAGeo);

    s.bodies.push_back(motorA_rotor);

    // Motor B Housing (Fixed on right of bedplate)
    RigidBody motorB_stator;
    motorB_stator.id = "motorB_stator";
    motorB_stator.name = "Motor Drive B Stator";
    motorB_stator.parentId = "bedplate";
    motorB_stator.localPosition = { 0.5, 0.2, 0.0 };
    VisualGeometry statorBGeo;
    statorBGeo.type = PrimitiveType::Cylinder;
    statorBGeo.size = { 0.18, 0.35, 0.18 };
    statorBGeo.colour = juce::Colour(0xff2d4052);
    motorB_stator.geometries.push_back(statorBGeo);
    s.bodies.push_back(motorB_stator);

    // Motor B Rotor & Shaft (Revolute joint around Z axis)
    Joint jointB;
    jointB.id = "joint_motorB";
    jointB.name = "Motor B Rotor Joint";
    jointB.type = JointType::Revolute;
    jointB.axis = { 0.0, 0.0, 1.0 };
    jointB.anchor = { 0.0, 0.0, 0.0 };
    jointB.binding.signalName = "motorB.angle";
    jointB.binding.fallbackSignal = "motorDriveB.angle";
    jointB.binding.scale = 1.0;
    s.joints.push_back(jointB);

    RigidBody motorB_rotor;
    motorB_rotor.id = "motorB_rotor";
    motorB_rotor.name = "Motor B Shaft & Wheel";
    motorB_rotor.parentId = "motorB_stator";
    motorB_rotor.jointId = "joint_motorB";
    motorB_rotor.localPosition = { 0.0, 0.0, 0.25 };

    VisualGeometry shaftBGeo;
    shaftBGeo.type = PrimitiveType::Cylinder;
    shaftBGeo.size = { 0.04, 0.2, 0.04 };
    shaftBGeo.colour = juce::Colour(0xffb0c4de);
    motorB_rotor.geometries.push_back(shaftBGeo);

    VisualGeometry flywheelBGeo;
    flywheelBGeo.type = PrimitiveType::Wheel;
    flywheelBGeo.size = { 0.25, 0.05, 0.25 };
    flywheelBGeo.offset = { 0.0, 0.0, 0.1 };
    flywheelBGeo.colour = juce::Colour(0xff80ed99);
    flywheelBGeo.accentColour = juce::Colour(0xffff6b6b);
    motorB_rotor.geometries.push_back(flywheelBGeo);

    s.bodies.push_back(motorB_rotor);

    s.evaluateTransforms();
    return s;
}

// Demonstration B: Controlled Mechanical Arm
MechanicalScene MechanicalScene::createControlledArmScene()
{
    MechanicalScene s;
    s.id = "controlled_arm_scene";
    s.name = "Articulated Robotic Arm (2-DOF)";
    s.description = "Two-link articulated mechanical arm driven by closed-loop position controllers with joint limits.";

    // Base Pedestal
    RigidBody base;
    base.id = "arm_base";
    base.name = "Base Pedestal";
    VisualGeometry baseGeo;
    baseGeo.type = PrimitiveType::Cylinder;
    baseGeo.size = { 0.25, 0.15, 0.25 };
    baseGeo.colour = juce::Colour(0xff18222b);
    base.geometries.push_back(baseGeo);
    s.bodies.push_back(base);

    // Joint 1: Shoulder (Rotates around Y/Z axis)
    Joint j1;
    j1.id = "joint_shoulder";
    j1.name = "Shoulder Joint";
    j1.type = JointType::Revolute;
    j1.axis = { 0.0, 0.0, 1.0 };
    j1.anchor = { 0.0, 0.1, 0.0 };
    j1.limits.enabled = true;
    j1.limits.minVal = -2.5;
    j1.limits.maxVal = 2.5;
    j1.binding.signalName = "arm.joint1.angle";
    j1.binding.fallbackSignal = "motorA.angle"; // or primary motor angle
    j1.binding.scale = 1.0;
    s.joints.push_back(j1);

    // Link 1 (Upper Arm)
    RigidBody link1;
    link1.id = "upper_arm";
    link1.name = "Upper Arm Link";
    link1.parentId = "arm_base";
    link1.jointId = "joint_shoulder";
    link1.localPosition = { 0.0, 0.15, 0.0 };

    VisualGeometry link1Geo;
    link1Geo.type = PrimitiveType::Box;
    link1Geo.size = { 0.08, 0.5, 0.08 };
    link1Geo.offset = { 0.0, 0.25, 0.0 };
    link1Geo.colour = juce::Colour(0xff35c6ff);
    link1.geometries.push_back(link1Geo);
    s.bodies.push_back(link1);

    // Joint 2: Elbow (Rotates at tip of Link 1)
    Joint j2;
    j2.id = "joint_elbow";
    j2.name = "Elbow Joint";
    j2.type = JointType::Revolute;
    j2.axis = { 0.0, 0.0, 1.0 };
    j2.anchor = { 0.0, 0.5, 0.0 };
    j2.limits.enabled = true;
    j2.limits.minVal = -2.8;
    j2.limits.maxVal = 2.8;
    j2.binding.signalName = "arm.joint2.angle";
    j2.binding.fallbackSignal = "motorB.angle"; // or secondary motor angle
    j2.binding.scale = 1.0;
    s.joints.push_back(j2);

    // Link 2 (Forearm & Tool)
    RigidBody link2;
    link2.id = "forearm";
    link2.name = "Forearm Link & Tool";
    link2.parentId = "upper_arm";
    link2.jointId = "joint_elbow";
    link2.localPosition = { 0.0, 0.5, 0.0 };

    VisualGeometry link2Geo;
    link2Geo.type = PrimitiveType::Box;
    link2Geo.size = { 0.06, 0.4, 0.06 };
    link2Geo.offset = { 0.0, 0.2, 0.0 };
    link2Geo.colour = juce::Colour(0xffffa630);
    link2.geometries.push_back(link2Geo);

    // End-effector pointer
    VisualGeometry toolGeo;
    toolGeo.type = PrimitiveType::Pointer;
    toolGeo.size = { 0.03, 0.1, 0.03 };
    toolGeo.offset = { 0.0, 0.45, 0.0 };
    toolGeo.colour = juce::Colour(0xffff6b6b);
    link2.geometries.push_back(toolGeo);

    s.bodies.push_back(link2);

    s.evaluateTransforms();
    return s;
}

} // namespace mechanical_scene
