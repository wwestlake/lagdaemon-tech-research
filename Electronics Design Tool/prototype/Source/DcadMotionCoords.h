#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dcad_motion
{

// -----------------------------------------------------------------------------
// Unit Conversions & Quantity Properties
// -----------------------------------------------------------------------------
double getUnitToSIFactor(const std::string& quantity, const std::string& unit);
bool isRateQuantity(const std::string& quantity);

// -----------------------------------------------------------------------------
// Profile Mapping & Errors
// -----------------------------------------------------------------------------
struct ProfileMapping
{
    std::string joint;
    std::string signal;
    std::string sourceQuantity;
    std::string sourceUnit;
    std::string jointQuantity;
    std::string jointUnit;
    double unitToSI = 1.0;
    double scale = 1.0;
    double offset = 0.0;
    bool hasInitialCoordinate = false;
    double initialCoordinate = 0.0;

    static ProfileMapping fromJson(const juce::var& json);
};

struct ValidationError
{
    std::string code;
    std::string joint;
    std::string signal;
    std::string message;

    juce::var toJson() const;
};

struct EvaluatedJoint
{
    std::string joint;
    double coordinate = 0.0;
    bool hasVelocity = false;
    double velocity = 0.0;

    juce::var toJson() const;
};

// -----------------------------------------------------------------------------
// Signal Descriptor (used for catalog validation)
// -----------------------------------------------------------------------------
struct MotionSignalDef
{
    std::string id;
    std::string name;
    std::string quantity;
    std::string unit;
    std::string kind;
    std::string companionSignal;
};

// -----------------------------------------------------------------------------
// Pure Functional Coordinate Math & Proposal Validation
// -----------------------------------------------------------------------------

// Validates proposed profile mappings against advertised catalog.
// Returns a list of validation errors (empty if valid).
std::vector<ValidationError> validateProposalMappings(
    const std::map<std::string, MotionSignalDef>& catalog,
    const std::vector<ProfileMapping>& mappings,
    bool isAlreadyTrackedOrInitial,
    const std::function<bool(const std::string& rateSignal)>& hasHistoricalIntegration = nullptr
);

// Computes joint coordinates from snapshot signals with atomic rollback semantics.
// - snapSignalValues: map of signal name -> current numeric value
// - snapRateIntegrals: simulation-owned rate integrals in SI (if available)
// - findCompanionFunc: lookup function returning declared companion signal name for a rate signal
// - getCompanionUnitToSI: lookup function returning unitToSI factor for a companion signal
// - inOutRateIntegrals: map of joint -> accumulated coordinate (staged and updated on success)
// - inOutCompanionOrigins: map of joint -> p_SI(t_start) (staged and updated on success)
// - outJoints: resulting evaluated joints on success
// - outError: detailed error string on failure
// Returns true on complete success. On failure, returns false and leaves inOutRateIntegrals/inOutCompanionOrigins UNMODIFIED.
bool evaluateJointCoordinates(
    const std::vector<ProfileMapping>& mappings,
    const std::map<std::string, double>& snapSignalValues,
    const std::map<std::string, double>& snapRateIntegrals,
    const std::function<std::string(const std::string&)>& findCompanionFunc,
    const std::function<double(const std::string&)>& getCompanionUnitToSI,
    std::map<std::string, double>& inOutRateIntegrals,
    std::map<std::string, double>& inOutCompanionOrigins,
    std::vector<EvaluatedJoint>& outJoints,
    std::string& outError
);

} // namespace dcad_motion
