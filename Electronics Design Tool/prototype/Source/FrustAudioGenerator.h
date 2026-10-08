#pragma once

#include "CircuitSolver.h"
#include <juce_core/juce_core.h>

class FrustAudioGenerator
{
public:
    enum class Method 
    { 
        Auto,       // Automatically select based on circuit linearity
        StateSpace, // Fast, strictly linear (R, C, L, ideal sources)
        MNA         // Slower, supports non-linear elements via Newton-Raphson
    };

    // Generates Frust source code that defines:
    // pub fn process_sample(audio_in: f64) -> f64
    static juce::String generate(const circuit_sim::Circuit& circuit, int audioInputNode, int audioOutputNode, Method method = Method::Auto);

private:
    static juce::String generateMNA(const circuit_sim::Circuit& circuit, int audioInputNode, int audioOutputNode);
    static juce::String generateStateSpace(const circuit_sim::Circuit& circuit, int audioInputNode, int audioOutputNode);
    static bool isStrictlyLinear(const circuit_sim::Circuit& circuit);
};
