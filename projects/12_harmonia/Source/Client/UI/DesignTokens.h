#pragma once
#include <juce_graphics/juce_graphics.h>

namespace Harmonia {
namespace UI {
    inline const juce::Colour kBgDeep     { 0xff050510 };
    inline const juce::Colour kBgMid      { 0xff0d0d2a };
    inline const juce::Colour kAccentCyan { 0xff00e5ff };
    inline const juce::Colour kAccentAmber{ 0xffffb300 };
    inline const juce::Colour kAccentGold { 0xffffd600 };
    inline const juce::Colour kTextPrimary  { 0xfff0f0ff };
    inline const juce::Colour kTextSecondary{ 0xff8888aa };
    
    inline constexpr int kPadSm = 8;
    inline constexpr int kPadMd = 16;
    inline constexpr int kPadLg = 32;
    
    inline juce::Font primaryFont(float size) { return juce::Font("Segoe UI", size, juce::Font::plain); }
    inline juce::Font monoFont(float size) { return juce::Font("Consolas", size, juce::Font::plain); }
}
}
