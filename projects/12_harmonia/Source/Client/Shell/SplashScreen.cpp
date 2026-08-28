#include "SplashScreen.h"
#include "Client/UI/DesignTokens.h"
#include "Shared/Music/CircleOfFifths.h"
#include <cmath>

namespace Harmonia {

SplashScreen::SplashScreen() {
    startTimerHz(60);
}

void SplashScreen::paint(juce::Graphics& g) {
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float alpha = juce::jlimit(0.0f, 1.0f, 1.0f - fadeOut_);

    // Deep space background with radial gradient
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff0a0a20), cx, cy,
        juce::Colour(0xff020208), 0.f, 0.f, true));
    g.fillAll();

    // ── Animated Circle of Fifths ring ─────────────────────────────────────
    const float ringR   = juce::jmin(w, h) * 0.28f;
    const float dotR    = ringR * 0.18f;
    const float spinOff = time_ * 0.15f;   // slow rotation

    for (int pc = 0; pc < 12; ++pc) {
        // Circle of fifths: C=0 at top, going clockwise by fifths
        float angle = (float)pc / 12.f * juce::MathConstants<float>::twoPi
                      + spinOff - juce::MathConstants<float>::halfPi;

        float px = cx + std::cos(angle) * ringR;
        float py = cy + std::sin(angle) * ringR;

        juce::Colour col = CircleOfFifths::colourForPitchClass(
            (pc * 7) % 12);  // map index to pitch class via circle of fifths

        // Pulse brightness based on harmonic_ and time
        float phase = time_ * 3.f - (float)pc * 0.52f;
        float bright = juce::jmax(0.0f, 0.5f + 0.5f * std::sin(phase));
        col = CircleOfFifths::withActivation(col, 0.4f + 0.6f * bright);

        g.setColour(col.withAlpha(juce::jlimit(0.0f, 1.0f, alpha * (0.7f + 0.3f * bright))));
        g.fillEllipse(px - dotR * 0.5f, py - dotR * 0.5f, dotR, dotR);

        // Glow halo
        g.setColour(col.withAlpha(juce::jlimit(0.0f, 1.0f, alpha * 0.15f * bright)));
        g.fillEllipse(px - dotR, py - dotR, dotR * 2.f, dotR * 2.f);
    }

    // ── Connecting arcs between consecutive fifths ─────────────────────────
    g.setColour(UI::kAccentCyan.withAlpha(juce::jlimit(0.0f, 1.0f, alpha * 0.12f)));
    juce::Path ring;
    ring.addEllipse(cx - ringR, cy - ringR, ringR * 2.f, ringR * 2.f);
    g.strokePath(ring, juce::PathStrokeType(1.5f));

    // ── Title ──────────────────────────────────────────────────────────────
    // Outer glow
    g.setFont(UI::primaryFont(56.f));
    for (float spread = 6.f; spread >= 1.f; spread -= 1.f) {
        g.setColour(UI::kAccentCyan.withAlpha(juce::jlimit(0.0f, 1.0f, alpha * 0.04f)));
        g.drawText("HARMONIA",
            juce::Rectangle<float>(cx - 200.f + spread, cy - 40.f + spread, 400.f, 60.f)
                .toNearestInt(),
            juce::Justification::centred);
    }
    g.setColour(UI::kTextPrimary.withAlpha(juce::jlimit(0.0f, 1.0f, alpha)));
    g.drawText("HARMONIA",
        juce::Rectangle<float>(cx - 200.f, cy - 40.f, 400.f, 60.f).toNearestInt(),
        juce::Justification::centred);

    // Subtitle
    g.setFont(UI::primaryFont(16.f));
    float subBlink = juce::jmax(0.0f, 0.5f + 0.5f * std::sin(time_ * 2.f));
    g.setColour(UI::kTextSecondary.withAlpha(juce::jlimit(0.0f, 1.0f, alpha * subBlink)));
    g.drawText("where music becomes space",
        juce::Rectangle<float>(cx - 200.f, cy + 28.f, 400.f, 24.f).toNearestInt(),
        juce::Justification::centred);

    // Click to continue (after 2s)
    if (time_ > 2.f && !completing_) {
        float blink = juce::jmax(0.0f, 0.5f + 0.5f * std::sin(time_ * 4.f));
        g.setFont(UI::primaryFont(13.f));
        g.setColour(UI::kTextSecondary.withAlpha(juce::jlimit(0.0f, 1.0f, alpha * blink * 0.8f)));

        g.drawText("click anywhere to continue",
            juce::Rectangle<float>(cx - 150.f, cy + 80.f, 300.f, 20.f).toNearestInt(),
            juce::Justification::centred);
    }
}

void SplashScreen::mouseDown(const juce::MouseEvent&) {
    if (time_ > 1.5f && !completing_)
        completing_ = true;
}

void SplashScreen::timerCallback() {
    time_ += 1.f / 60.f;

    // Auto-advance after 5 seconds
    if (time_ > 5.f && !completing_)
        completing_ = true;

    if (completing_) {
        fadeOut_ += 0.04f;
        if (fadeOut_ >= 1.f) {
            stopTimer();
            if (onComplete) {
                juce::MessageManager::getInstance()->callAsync(onComplete);
            }
            return; // Important: do not call repaint() as we are being deleted
        }
    }
    repaint();
}

} // namespace Harmonia
