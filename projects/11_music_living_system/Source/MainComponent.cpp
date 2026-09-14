#include "MainComponent.h"

MainComponent::MainComponent()
{
    // ── Voxel world ──────────────────────────────────────────────────────────
    // Grid dimensions: X=12 (pitch classes), Y=7 (octaves), Z=32 (beat slots)
    voxelGrid   = std::make_shared<VoxelGrid>(12, 7, 32);
    caEngine    = std::make_unique<CAEngine>(voxelGrid);
    musicMapper = std::make_unique<MusicMapper>(voxelGrid);

    // Seed with a sparse random pattern to start
    voxelGrid->seedRandom(0.06f);

    // ── Audio ─────────────────────────────────────────────────────────────────
    midiEngine = std::make_unique<MidiEngine>();
    musicMapper->setNoteCallback([this](const MusicMapper::NoteEvent& ev) {
        midiEngine->sendNote(ev);
    });

    // ── Rendering ────────────────────────────────────────────────────────────
    glView = std::make_unique<OpenGLView>(voxelGrid);
    addAndMakeVisible(*glView);

    // ── Control panel ─────────────────────────────────────────────────────────
    controlPanel = std::make_unique<ControlPanel>();
    addAndMakeVisible(*controlPanel);

    controlPanel->onBpmChanged = [this](double newBpm) {
        bpm = newBpm;
        startTimer(static_cast<int>(generationIntervalMs()));
        glView->setBpm(newBpm);
    };
    controlPanel->onRuleChanged = [this](CAEngine::RuleType rule) {
        caEngine->setRule(rule);
    };
    controlPanel->onParamsChanged = [this](const CAEngine::Params& p) {
        caEngine->setParams(p);
    };
    controlPanel->onSeedRandom = [this]() {
        voxelGrid->seedRandom(0.06f);
    };
    controlPanel->onClear = [this]() {
        voxelGrid->clear();
    };

    setSize(1400, 900);

    // Start the CA generation timer
    startTimer(static_cast<int>(generationIntervalMs()));
}

MainComponent::~MainComponent()
{
    stopTimer();
    // OpenGLView must be destroyed before VoxelGrid (it holds a reference)
    glView.reset();
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    controlPanel->setBounds(bounds.removeFromRight(280));
    glView->setBounds(bounds);
}

void MainComponent::timerCallback()
{
    // Advance the CA one generation
    caEngine->step();

    // Advance sweep plane position and fire notes for the current Z slice
    const int z = glView->advanceSweep();
    musicMapper->triggerSlice(z, midiEngine->currentTimestamp());

    glView->repaint();
}
