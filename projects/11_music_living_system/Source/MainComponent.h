#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Rendering/OpenGLView.h"
#include "Audio/MidiEngine.h"
#include "UI/ControlPanel.h"
#include "VoxelWorld/VoxelGrid.h"
#include "VoxelWorld/CAEngine.h"
#include "VoxelWorld/MusicMapper.h"

//==============================================================================
/**
 * MainComponent — top-level JUCE component.
 *
 * Layout:
 *   ┌──────────────────────────────┬──────────────┐
 *   │                              │              │
 *   │       OpenGLView (3D)        │ControlPanel  │
 *   │                              │              │
 *   └──────────────────────────────┴──────────────┘
 *
 * The CA engine runs on a background timer thread, updating the
 * VoxelGrid each generation. The MusicMapper translates grid events
 * to MIDI. The OpenGLView renders the grid at 60fps.
 */
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint(juce::Graphics&) override {}

private:
    void timerCallback() override;   // CA generation tick

    // Core data — shared between threads (VoxelGrid is internally atomic)
    std::shared_ptr<VoxelGrid>   voxelGrid;
    std::unique_ptr<CAEngine>    caEngine;
    std::unique_ptr<MusicMapper> musicMapper;

    // Audio
    std::unique_ptr<MidiEngine>  midiEngine;

    // UI
    std::unique_ptr<OpenGLView>  glView;
    std::unique_ptr<ControlPanel> controlPanel;

    // Generation state
    double bpm         = 120.0;
    int    subdivisions = 4;    // 16th notes within a bar
    double generationIntervalMs() const { return (60000.0 / bpm) / subdivisions; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
