#include <juce_core/juce_core.h>
#include "DcadMotionCoords.h"
#include <iostream>
#include <cmath>
#include <chrono>

namespace {

int totalChecks = 0;
int passedChecks = 0;

#define TEST_CHECK(condition, message) \
    do { \
        ++totalChecks; \
        if (condition) { \
            ++passedChecks; \
        } else { \
            std::cerr << "FAILED: " << message << " (" << #condition << ")" << std::endl; \
        } \
    } while (0)

} // namespace

int main()
{
    auto startTime = std::chrono::steady_clock::now();
    std::cout << "=== Running Fast Lightweight DCAD Coordinate Unit Tests ===" << std::endl;

    // -------------------------------------------------------------------------
    // 1. Unit conversions and Rate quantity classifications
    // -------------------------------------------------------------------------
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angle", "rad") - 1.0) < 1e-12, "angle rad factor is 1.0");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angle", "deg") - (3.14159265358979323846 / 180.0)) < 1e-12, "angle deg factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angular_velocity", "rpm") - (2.0 * 3.14159265358979323846 / 60.0)) < 1e-12, "angular_velocity rpm factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("position", "mm") - 0.001) < 1e-12, "position mm factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("linear_velocity", "mm/s") - 0.001) < 1e-12, "linear_velocity mm/s factor");
    TEST_CHECK(dcad_motion::isRateQuantity("angular_velocity"), "angular_velocity is rate");
    TEST_CHECK(dcad_motion::isRateQuantity("linear_velocity"), "linear_velocity is rate");
    TEST_CHECK(!dcad_motion::isRateQuantity("position"), "position is not rate");
    TEST_CHECK(!dcad_motion::isRateQuantity("angle"), "angle is not rate");

    // -------------------------------------------------------------------------
    // 2. Proposal Validation
    // -------------------------------------------------------------------------
    std::map<std::string, dcad_motion::MotionSignalDef> catalog;
    catalog["motor.speed"] = { "motor.speed", "Speed", "angular_velocity", "rad/s", "rate", "motor.angle" };
    catalog["motor.angle"] = { "motor.angle", "Angle", "angle", "rad", "state", "" };
    catalog["load.torque"] = { "load.torque", "Torque", "torque", "N*m", "state", "" };

    // Valid proposal
    std::vector<dcad_motion::ProfileMapping> validMappings;
    dcad_motion::ProfileMapping m1;
    m1.joint = "joint-motor";
    m1.signal = "motor.speed";
    m1.sourceQuantity = "angular_velocity";
    m1.sourceUnit = "rad/s";
    m1.jointQuantity = "angle";
    m1.jointUnit = "rad";
    m1.unitToSI = 1.0;
    m1.scale = 1.0;
    m1.offset = 0.0;
    m1.hasInitialCoordinate = true;
    m1.initialCoordinate = 0.5; // Non-zero initial coordinate
    validMappings.push_back(m1);

    auto valErrors = dcad_motion::validateProposalMappings(catalog, validMappings, true);
    TEST_CHECK(valErrors.empty(), "Valid proposal accepted with no validation errors");

    // Unknown signal rejection
    std::vector<dcad_motion::ProfileMapping> badMappings = validMappings;
    badMappings[0].signal = "carriage.position";
    badMappings[0].sourceQuantity = "position";
    badMappings[0].sourceUnit = "mm";
    auto errsUnknown = dcad_motion::validateProposalMappings(catalog, badMappings, true);
    TEST_CHECK(!errsUnknown.empty() && errsUnknown[0].code == "unknownSignal", "Unadvertised signal rejected with unknownSignal");

    // Quantity mismatch rejection
    badMappings = validMappings;
    badMappings[0].sourceQuantity = "linear_velocity";
    auto errsQty = dcad_motion::validateProposalMappings(catalog, badMappings, true);
    TEST_CHECK(!errsQty.empty() && errsQty[0].code == "quantityMismatch", "Quantity mismatch rejected with quantityMismatch");

    // Unit-to-SI mismatch rejection
    badMappings = validMappings;
    badMappings[0].unitToSI = 0.5; // incorrect factor for rad/s
    auto errsUnit = dcad_motion::validateProposalMappings(catalog, badMappings, true);
    TEST_CHECK(!errsUnit.empty() && errsUnit[0].code == "unitMismatch", "Unit-to-SI factor mismatch rejected with unitMismatch");

    // Non-finite scale rejection
    badMappings = validMappings;
    badMappings[0].scale = 0.0;
    auto errsScale = dcad_motion::validateProposalMappings(catalog, badMappings, true);
    TEST_CHECK(!errsScale.empty() && errsScale[0].code == "nonFiniteValue", "Zero/non-finite scale rejected with nonFiniteValue");

    // Mid-run rate binding without companion or historical integration rejection
    std::map<std::string, dcad_motion::MotionSignalDef> soloRateCatalog;
    soloRateCatalog["solo.speed"] = { "solo.speed", "Solo Speed", "angular_velocity", "rad/s", "rate", "" };
    std::vector<dcad_motion::ProfileMapping> soloMappings;
    dcad_motion::ProfileMapping sm;
    sm.joint = "joint-solo";
    sm.signal = "solo.speed";
    sm.sourceQuantity = "angular_velocity";
    sm.sourceUnit = "rad/s";
    sm.jointQuantity = "angle";
    sm.jointUnit = "rad";
    sm.unitToSI = 1.0;
    sm.scale = 1.0;
    sm.hasInitialCoordinate = true;
    sm.initialCoordinate = 0.0;
    soloMappings.push_back(sm);

    auto errsNoHist = dcad_motion::validateProposalMappings(soloRateCatalog, soloMappings, false, nullptr);
    TEST_CHECK(!errsNoHist.empty() && errsNoHist[0].code == "integrationUnavailable",
               "Mid-run rate source without companion/historical integration rejected with integrationUnavailable");

    // -------------------------------------------------------------------------
    // 3. Initial Coordinate Handling & Companion Position Origin Tracking
    // Contract: q(t) = initialCoordinate + scale * (companion_SI(t) - companion_SI(t_start))
    // -------------------------------------------------------------------------
    std::map<std::string, double> rateIntegrals;
    std::map<std::string, double> companionOrigins;
    std::vector<dcad_motion::EvaluatedJoint> evaluated;
    std::string evalError;

    auto findCompanion = [](const std::string& sig) -> std::string {
        if (sig == "motor.speed") return "motor.angle";
        return "";
    };
    auto getCompFactor = [](const std::string&) -> double {
        return 1.0;
    };

    // Step A: Timeline start at t = 0 (before motion, motor.angle = 0.0 rad)
    std::map<std::string, double> snapSignals {
        { "motor.speed", 0.0 },
        { "motor.angle", 0.0 }
    };
    std::map<std::string, double> snapRateIntegrals;

    bool okA = dcad_motion::evaluateJointCoordinates(
        validMappings, snapSignals, snapRateIntegrals, findCompanion, getCompFactor,
        rateIntegrals, companionOrigins, evaluated, evalError
    );
    TEST_CHECK(okA, "Step A coordinate evaluation succeeded");
    TEST_CHECK(evaluated.size() == 1, "Step A produced 1 joint");
    if (evaluated.size() == 1)
    {
        TEST_CHECK(std::abs(evaluated[0].coordinate - 0.5) < 1e-12,
                   "Initial coordinate 0.5 rad preserved exactly before motion at timeline origin");
        TEST_CHECK(std::abs(evaluated[0].velocity - 0.0) < 1e-12, "Initial velocity is 0.0");
    }

    // Step B: Motion occurs (motor.angle advances to 1.75 rad, motor.speed = 100 rad/s)
    snapSignals["motor.speed"] = 100.0;
    snapSignals["motor.angle"] = 1.75;

    bool okB = dcad_motion::evaluateJointCoordinates(
        validMappings, snapSignals, snapRateIntegrals, findCompanion, getCompFactor,
        rateIntegrals, companionOrigins, evaluated, evalError
    );
    TEST_CHECK(okB, "Step B coordinate evaluation succeeded");
    if (evaluated.size() == 1)
    {
        // Expected coordinate: 0.5 + 1.0 * (1.75 - 0.0) = 2.25 rad
        TEST_CHECK(std::abs(evaluated[0].coordinate - 2.25) < 1e-12,
                   "Joint coordinate correctly equals initialCoordinate (0.5) + displacement (1.75) = 2.25 rad");
        TEST_CHECK(std::abs(evaluated[0].velocity - 100.0) < 1e-12, "Joint velocity is 100.0 rad/s");
    }

    // -------------------------------------------------------------------------
    // 4. Rate-Only Source with Simulation-Owned Integration
    // Contract: q(t) = initialCoordinate + scale * (integratedRate_SI)
    // -------------------------------------------------------------------------
    std::vector<dcad_motion::ProfileMapping> rateOnlyMappings;
    dcad_motion::ProfileMapping r1;
    r1.joint = "joint-rate-only";
    r1.signal = "flow.rate";
    r1.sourceQuantity = "angular_velocity";
    r1.sourceUnit = "rad/s";
    r1.jointQuantity = "angle";
    r1.jointUnit = "rad";
    r1.unitToSI = 1.0;
    r1.scale = 2.0; // scale 2x
    r1.hasInitialCoordinate = true;
    r1.initialCoordinate = 10.0; // initial 10 rad
    rateOnlyMappings.push_back(r1);

    std::map<std::string, double> rateOnlySignals {
        { "flow.rate", 5.0 }
    };
    std::map<std::string, double> solverIntegrals {
        { "flow.rate", 3.5 } // solver accumulated 3.5 rad in simulation time
    };
    std::map<std::string, double> rateIntegrals2;
    std::map<std::string, double> companionOrigins2;

    bool okRate = dcad_motion::evaluateJointCoordinates(
        rateOnlyMappings, rateOnlySignals, solverIntegrals, nullptr, nullptr,
        rateIntegrals2, companionOrigins2, evaluated, evalError
    );
    TEST_CHECK(okRate, "Simulation-owned rate evaluation succeeded");
    if (evaluated.size() == 1)
    {
        // Expected: initialCoordinate (10.0) + scale (2.0) * solverIntegral (3.5) = 17.0 rad
        TEST_CHECK(std::abs(evaluated[0].coordinate - 17.0) < 1e-12,
                   "Simulation-owned rate integral correctly evaluated: 10.0 + 2.0 * 3.5 = 17.0 rad");
        TEST_CHECK(std::abs(evaluated[0].velocity - 10.0) < 1e-12, "Velocity scaled by 2.0: 5.0 * 2.0 = 10.0 rad/s");
    }

    // -------------------------------------------------------------------------
    // 5. Atomic Rollback on Evaluation Failure
    // If ANY mapping cannot be evaluated (e.g. signal missing or NaN), evaluation must
    // return false and leave inOutRateIntegrals / inOutCompanionOrigins UNMODIFIED.
    // -------------------------------------------------------------------------
    std::vector<dcad_motion::ProfileMapping> multiMappings;
    multiMappings.push_back(m1); // valid motor.speed
    dcad_motion::ProfileMapping mBroken;
    mBroken.joint = "joint-broken";
    mBroken.signal = "missing.sensor";
    mBroken.sourceQuantity = "position";
    mBroken.sourceUnit = "m";
    mBroken.jointQuantity = "position";
    mBroken.jointUnit = "m";
    mBroken.unitToSI = 1.0;
    mBroken.scale = 1.0;
    multiMappings.push_back(mBroken);

    auto savedRateIntegrals = rateIntegrals;
    auto savedCompanionOrigins = companionOrigins;

    bool okBroken = dcad_motion::evaluateJointCoordinates(
        multiMappings, snapSignals, snapRateIntegrals, findCompanion, getCompFactor,
        rateIntegrals, companionOrigins, evaluated, evalError
    );
    TEST_CHECK(!okBroken, "Evaluation failed on missing signal as expected");
    TEST_CHECK(evaluated.empty(), "Evaluated joints cleared on failure");
    TEST_CHECK(rateIntegrals == savedRateIntegrals, "Atomic rollback preserved rateIntegrals exactly");
    TEST_CHECK(companionOrigins == savedCompanionOrigins, "Atomic rollback preserved companionOrigins exactly");

    // Also test non-finite (NaN) signal value
    std::map<std::string, double> nanSignals = snapSignals;
    nanSignals["motor.speed"] = std::numeric_limits<double>::quiet_NaN();
    bool okNaN = dcad_motion::evaluateJointCoordinates(
        validMappings, nanSignals, snapRateIntegrals, findCompanion, getCompFactor,
        rateIntegrals, companionOrigins, evaluated, evalError
    );
    TEST_CHECK(!okNaN, "Evaluation failed on NaN signal value as expected");
    TEST_CHECK(rateIntegrals == savedRateIntegrals, "Atomic rollback preserved state across NaN failure");

    auto endTime = std::chrono::steady_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    std::cout << "\nTest Summary: " << passedChecks << " / " << totalChecks << " checks passed in "
              << elapsedMs << " ms." << std::endl;
    return (passedChecks == totalChecks) ? 0 : 1;
}
