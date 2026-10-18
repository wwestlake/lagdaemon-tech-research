#pragma once

#include "PlotInstrument.h"
#include "SystemSimulationSession.h"

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mechanical_scene
{

// -----------------------------------------------------------------------------
// 3D Math Primitives
// -----------------------------------------------------------------------------

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(double s) const { return { x * s, y * s, z * s }; }

    double length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const
    {
        double l = length();
        return l > 1e-12 ? (*this) * (1.0 / l) : Vec3(0, 0, 1);
    }

    juce::var toJson() const
    {
        juce::Array<juce::var> arr;
        arr.add(x); arr.add(y); arr.add(z);
        return juce::var(arr);
    }

    static Vec3 fromJson(const juce::var& v, const Vec3& fallback = {})
    {
        if (const auto* arr = v.getArray())
        {
            if (arr->size() >= 3)
                return { (double)(*arr)[0], (double)(*arr)[1], (double)(*arr)[2] };
        }
        return fallback;
    }
};

struct Mat4
{
    double m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    static Mat4 identity() { return Mat4(); }

    static Mat4 translate(const Vec3& v)
    {
        Mat4 r;
        r.m[12] = v.x;
        r.m[13] = v.y;
        r.m[14] = v.z;
        return r;
    }

    static Mat4 scale(const Vec3& s)
    {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }

    // Rotation around unit axis (Rodrigues / axis-angle)
    static Mat4 rotateAxis(const Vec3& axis, double radians)
    {
        Vec3 a = axis.normalized();
        double c = std::cos(radians);
        double s = std::sin(radians);
        double t = 1.0 - c;

        Mat4 r;
        r.m[0] = c + a.x * a.x * t;
        r.m[1] = a.y * a.x * t + a.z * s;
        r.m[2] = a.z * a.x * t - a.y * s;
        r.m[3] = 0.0;

        r.m[4] = a.x * a.y * t - a.z * s;
        r.m[5] = c + a.y * a.y * t;
        r.m[6] = a.z * a.y * t + a.x * s;
        r.m[7] = 0.0;

        r.m[8]  = a.x * a.z * t + a.y * s;
        r.m[9]  = a.y * a.z * t - a.x * s;
        r.m[10] = c + a.z * a.z * t;
        r.m[11] = 0.0;

        r.m[12] = 0.0;
        r.m[13] = 0.0;
        r.m[14] = 0.0;
        r.m[15] = 1.0;
        return r;
    }

    Mat4 operator*(const Mat4& o) const
    {
        Mat4 res;
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k)
                    sum += m[k * 4 + r] * o.m[c * 4 + k];
                res.m[c * 4 + r] = sum;
            }
        }
        return res;
    }

    Vec3 transformPoint(const Vec3& p) const
    {
        double x = m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12];
        double y = m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13];
        double z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        double w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (std::abs(w) > 1e-12 && std::abs(w - 1.0) > 1e-9)
        {
            double inv = 1.0 / w;
            return { x * inv, y * inv, z * inv };
        }
        return { x, y, z };
    }

    Vec3 transformVector(const Vec3& v) const
    {
        return {
            m[0] * v.x + m[4] * v.y + m[8]  * v.z,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z
        };
    }
};

// -----------------------------------------------------------------------------
// Visual Geometry Primitives
// -----------------------------------------------------------------------------

enum class PrimitiveType
{
    Box,        // dimensions: width(x), height(y), depth(z)
    Cylinder,   // radius(x), height(y), axis along Y (or Z)
    Sphere,     // radius(x)
    Wheel,      // disc with rim and indicator mark
    Pointer     // indicator needle / dial
};

struct VisualGeometry
{
    PrimitiveType type = PrimitiveType::Box;
    Vec3 size { 1.0, 1.0, 1.0 };          // Box: half-extents or (diam, length, diam)
    Vec3 offset { 0.0, 0.0, 0.0 };        // local geometric offset relative to body origin
    juce::Colour colour { 0xff70889b };
    juce::Colour accentColour { 0xff35c6ff };

    juce::var toJson() const;
    static VisualGeometry fromJson(const juce::var& json);
};

// -----------------------------------------------------------------------------
// Joints and Rigid Bodies
// -----------------------------------------------------------------------------

enum class JointType
{
    Fixed,      // rigid attachment
    Revolute,   // rotational: angle around axis (rad)
    Prismatic   // linear: displacement along axis (m)
};

struct JointLimit
{
    bool enabled = false;
    double minVal = -3.1415926535;
    double maxVal = 3.1415926535;
};

// Binding connecting a simulation signal (e.g. "motorA.angle", "motorA.speed")
// to a joint's angle or displacement
struct SignalBinding
{
    std::string signalName;     // e.g. "motorDriveA.angle", "motorDriveA.speed", "arm.joint1.angle"
    double scale = 1.0;         // multiplier: e.g. 1.0 for radians, 0.01 for gearing/scaling
    double offset = 0.0;        // bias
    bool isVelocity = false;    // if true, integrates velocity over dt if direct angle is not published
    std::string fallbackSignal; // optional secondary signal

    juce::var toJson() const;
    static SignalBinding fromJson(const juce::var& json);
};

struct Joint
{
    std::string id;
    std::string name;
    JointType type = JointType::Fixed;
    Vec3 axis { 0.0, 0.0, 1.0 };          // rotation or translation axis
    Vec3 anchor { 0.0, 0.0, 0.0 };        // anchor point relative to parent body
    JointLimit limits;

    // Simulation signal binding
    SignalBinding binding;

    // Dynamic state evaluated from simulation
    double currentValue = 0.0;            // angle in radians or linear translation in m
    double currentVelocity = 0.0;         // rad/s or m/s

    juce::var toJson() const;
    static Joint fromJson(const juce::var& json);
};

struct RigidBody
{
    std::string id;
    std::string name;
    std::string parentId;                 // empty if base/root body
    std::string jointId;                  // joint connecting this body to parentId

    Vec3 localPosition { 0.0, 0.0, 0.0 }; // resting translation relative to parent
    Vec3 localRotation { 0.0, 0.0, 0.0 }; // resting Euler angles (radians)

    std::vector<VisualGeometry> geometries;

    // Computed world transform for rendering
    Mat4 worldTransform = Mat4::identity();

    juce::var toJson() const;
    static RigidBody fromJson(const juce::var& json);
};

// -----------------------------------------------------------------------------
// Mechanical Scene
// -----------------------------------------------------------------------------

struct MechanicalScene
{
    std::string id = "mechanical_scene";
    std::string name = "Mechanical Visualization Scene";
    std::string description;

    std::vector<RigidBody> bodies;
    std::vector<Joint> joints;

    // Query helpers
    const RigidBody* findBody(const std::string& bodyId) const;
    RigidBody* findBody(const std::string& bodyId);
    const Joint* findJoint(const std::string& jointId) const;
    Joint* findJoint(const std::string& jointId);

    // Tree / Hierarchy evaluation: computes worldTransform for every body
    void evaluateTransforms();

    // Update joint values from a simulation snapshot
    void updateFromSimulation(const sim_panel::StateSnapshot& snapshot, double dtSeconds = 0.0);

    // Reset all joint values to neutral/rest pose
    void resetToRestPose();

    // JSON serialization
    juce::var toJson() const;
    static MechanicalScene fromJson(const juce::var& json);

    // Standard factory scenes
    static MechanicalScene createDualMotorShaftScene();
    static MechanicalScene createControlledArmScene();
};

} // namespace mechanical_scene
