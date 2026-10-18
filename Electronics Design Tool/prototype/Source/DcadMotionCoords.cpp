#include "DcadMotionCoords.h"
#include <cmath>

namespace dcad_motion
{

// -----------------------------------------------------------------------------
// Unit Conversions & Quantity Properties
// -----------------------------------------------------------------------------
double getUnitToSIFactor(const std::string& quantity, const std::string& unit)
{
    static const double kPi = 3.14159265358979323846;
    if (quantity == "angle")
    {
        if (unit == "rad") return 1.0;
        if (unit == "deg") return kPi / 180.0;
        if (unit == "rev") return 2.0 * kPi;
    }
    else if (quantity == "angular_velocity")
    {
        if (unit == "rad/s") return 1.0;
        if (unit == "deg/s") return kPi / 180.0;
        if (unit == "rpm") return 2.0 * kPi / 60.0;
        if (unit == "rev/s") return 2.0 * kPi;
    }
    else if (quantity == "position")
    {
        if (unit == "m") return 1.0;
        if (unit == "mm") return 0.001;
        if (unit == "um") return 1e-6;
        if (unit == "in") return 0.0254;
    }
    else if (quantity == "linear_velocity")
    {
        if (unit == "m/s") return 1.0;
        if (unit == "mm/s") return 0.001;
        if (unit == "in/s") return 0.0254;
    }
    return 1.0;
}

bool isRateQuantity(const std::string& quantity)
{
    return (quantity == "angular_velocity" || quantity == "linear_velocity");
}

// -----------------------------------------------------------------------------
// JSON Serialization
// -----------------------------------------------------------------------------
ProfileMapping ProfileMapping::fromJson(const juce::var& json)
{
    ProfileMapping m;
    m.joint = json.getProperty("joint", {}).toString().toStdString();
    m.signal = json.getProperty("signal", {}).toString().toStdString();
    m.sourceQuantity = json.getProperty("sourceQuantity", {}).toString().toStdString();
    m.sourceUnit = json.getProperty("sourceUnit", {}).toString().toStdString();
    m.jointQuantity = json.getProperty("jointQuantity", {}).toString().toStdString();
    m.jointUnit = json.getProperty("jointUnit", {}).toString().toStdString();
    m.unitToSI = (double)json.getProperty("unitToSI", 1.0);
    m.scale = (double)json.getProperty("scale", 1.0);
    m.offset = (double)json.getProperty("offset", 0.0);

    auto initCoordVar = json.getProperty("initialCoordinate", {});
    if (!initCoordVar.isVoid())
    {
        m.hasInitialCoordinate = true;
        m.initialCoordinate = (double)initCoordVar;
    }
    return m;
}

juce::var ValidationError::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("code", juce::String(code));
    if (!joint.empty()) obj->setProperty("joint", juce::String(joint));
    if (!signal.empty()) obj->setProperty("signal", juce::String(signal));
    obj->setProperty("message", juce::String(message));
    return juce::var(obj);
}

juce::var EvaluatedJoint::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("joint", juce::String(joint));
    obj->setProperty("coordinate", coordinate);
    if (hasVelocity)
        obj->setProperty("velocity", velocity);
    return juce::var(obj);
}

// -----------------------------------------------------------------------------
// Proposal Validation
// -----------------------------------------------------------------------------
std::vector<ValidationError> validateProposalMappings(
    const std::map<std::string, MotionSignalDef>& catalog,
    const std::vector<ProfileMapping>& mappings,
    bool isAlreadyTrackedOrInitial,
    const std::function<bool(const std::string& rateSignal)>& hasHistoricalIntegration)
{
    std::vector<ValidationError> errors;

    if (mappings.empty())
    {
        errors.push_back({ "emptyProfile", "", "", "Profile mapping list cannot be empty." });
        return errors;
    }

    for (const auto& m : mappings)
    {
        auto it = catalog.find(m.signal);
        if (it == catalog.end())
        {
            errors.push_back({ "unknownSignal", m.joint, m.signal, "Signal does not exist in advertised interface." });
            continue;
        }

        const auto& sig = it->second;
        if (sig.quantity != m.sourceQuantity || sig.unit != m.sourceUnit)
        {
            errors.push_back({ "quantityMismatch", m.joint, m.signal, "Signal quantity/unit mismatch with mapping." });
            continue;
        }

        const double expectedFactor = getUnitToSIFactor(sig.quantity, sig.unit);
        if (std::abs(expectedFactor - m.unitToSI) > 1e-12)
        {
            errors.push_back({ "unitMismatch", m.joint, m.signal, "Unit-to-SI factor does not match protocol conversion table." });
            continue;
        }

        if (std::isnan(m.scale) || std::isinf(m.scale) || std::abs(m.scale) < 1e-15)
        {
            errors.push_back({ "nonFiniteValue", m.joint, m.signal, "Scale factor must be non-zero finite number." });
            continue;
        }

        // Check historical integration availability for rate sources
        if (isRateQuantity(m.sourceQuantity))
        {
            bool hasHist = false;
            if (!sig.companionSignal.empty())
                hasHist = true;
            else if (hasHistoricalIntegration && hasHistoricalIntegration(m.signal))
                hasHist = true;

            if (!isAlreadyTrackedOrInitial && !hasHist)
            {
                errors.push_back({ "integrationUnavailable", m.joint, m.signal,
                    "Historical integration unavailable for rate source mid-run; map an authoritative state signal or reset simulation." });
                continue;
            }
        }
    }

    return errors;
}

// -----------------------------------------------------------------------------
// Coordinate Evaluation with Atomic Rollback
// -----------------------------------------------------------------------------
bool evaluateJointCoordinates(
    const std::vector<ProfileMapping>& mappings,
    const std::map<std::string, double>& snapSignalValues,
    const std::map<std::string, double>& snapRateIntegrals,
    const std::function<std::string(const std::string&)>& findCompanionFunc,
    const std::function<double(const std::string&)>& getCompanionUnitToSI,
    std::map<std::string, double>& inOutRateIntegrals,
    std::map<std::string, double>& inOutCompanionOrigins,
    std::vector<EvaluatedJoint>& outJoints,
    std::string& outError)
{
    outJoints.clear();
    outError.clear();

    // Work on local copies to guarantee atomic rollback if ANY joint fails
    auto stagedRateIntegrals = inOutRateIntegrals;
    auto stagedCompanionOrigins = inOutCompanionOrigins;
    std::vector<EvaluatedJoint> tempJoints;
    tempJoints.reserve(mappings.size());

    for (const auto& m : mappings)
    {
        EvaluatedJoint ej;
        ej.joint = m.joint;

        auto it = snapSignalValues.find(m.signal);
        if (it == snapSignalValues.end())
        {
            outError = "Bound signal not present in simulation snapshot: " + m.signal;
            return false;
        }

        double rawVal = it->second;
        if (!std::isfinite(rawVal))
        {
            outError = "Bound signal has non-finite value in simulation snapshot: " + m.signal;
            return false;
        }

        const double unitValSI = rawVal * m.unitToSI;
        const double scaledSI = m.scale * unitValSI;

        if (isRateQuantity(m.sourceQuantity))
        {
            std::string companion;
            if (findCompanionFunc)
                companion = findCompanionFunc(m.signal);

            if (!companion.empty())
            {
                auto itComp = snapSignalValues.find(companion);
                if (itComp == snapSignalValues.end())
                {
                    outError = "Declared companion position signal not present in simulation snapshot: " + companion;
                    return false;
                }
                double compRaw = itComp->second;
                if (!std::isfinite(compRaw))
                {
                    outError = "Declared companion position signal has non-finite value: " + companion;
                    return false;
                }

                double compUnitToSI = 1.0;
                if (getCompanionUnitToSI)
                    compUnitToSI = getCompanionUnitToSI(companion);

                const double compSI = compRaw * compUnitToSI;

                if (stagedCompanionOrigins.find(m.joint) == stagedCompanionOrigins.end())
                {
                    stagedCompanionOrigins[m.joint] = compSI;
                }
                const double compOriginSI = stagedCompanionOrigins[m.joint];
                const double initCoord = m.hasInitialCoordinate ? m.initialCoordinate : 0.0;

                // q(t) = initialCoordinate + scale * (p_SI(t) - p_SI(t_start))
                ej.coordinate = initCoord + m.scale * (compSI - compOriginSI);
                ej.hasVelocity = true;
                ej.velocity = scaledSI;
                stagedRateIntegrals[m.joint] = ej.coordinate;
            }
            else
            {
                // Consume simulation-owned rate integral from solver
                const double initCoord = m.hasInitialCoordinate ? m.initialCoordinate : 0.0;
                auto itInt = snapRateIntegrals.find(m.signal);
                if (itInt != snapRateIntegrals.end() && std::isfinite(itInt->second))
                {
                    const double integratedRateSI = itInt->second * m.unitToSI;
                    ej.coordinate = initCoord + m.scale * integratedRateSI;
                }
                else
                {
                    // Fallback to tracked coordinate if snapshot has no integral entry yet
                    ej.coordinate = stagedRateIntegrals.count(m.joint) ? stagedRateIntegrals[m.joint] : initCoord;
                }

                ej.hasVelocity = true;
                ej.velocity = scaledSI;
                stagedRateIntegrals[m.joint] = ej.coordinate;
            }
        }
        else
        {
            // State source: q(t) = offset + scale * position_SI
            ej.coordinate = m.offset + scaledSI;
            ej.hasVelocity = false;
            ej.velocity = 0.0;
            stagedRateIntegrals[m.joint] = ej.coordinate;
        }

        tempJoints.push_back(ej);
    }

    // Success for all joints: commit staged values atomically
    inOutRateIntegrals = std::move(stagedRateIntegrals);
    inOutCompanionOrigins = std::move(stagedCompanionOrigins);
    outJoints = std::move(tempJoints);
    return true;
}

} // namespace dcad_motion
