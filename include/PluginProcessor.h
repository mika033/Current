#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <atomic>
#include "ModuleTypes.h"
#include "Engine.h"

// Parameter ids for the global settings. Central so the editor's combos and the
// processor's APVTS layout can't drift.
namespace ParamIDs
{
    constexpr auto root     = "root";
    constexpr auto scale    = "scale";
    constexpr auto quantize = "quantize";
    constexpr auto theme    = "theme";
}

// One placed module on the canvas. Position is stored in canvas coordinates
// (top-left of the node). Phase 2 has no per-module settings yet, so type +
// position is the whole model.
struct ModuleInstance
{
    int        id = 0;
    ModuleType type = ModuleType::Arp;
    float      x = 0.0f;
    float      y = 0.0f;
};

// Phase 2 processor: a MIDI effect whose processBlock produces no audio and
// drives the Engine (fixed implicit module chain, see Engine.h). The module
// layout lives here (not in the editor) so it survives the editor being closed
// and reopened and is persisted in the DAW project state.
//
// Threading: the module list is touched only from the message thread (editor +
// canvas). The audio thread never reads it directly — it reads a lock-free
// presence snapshot (per-module-type atomics) republished by
// refreshEngineConfig() after every model change. Once real port wiring lands
// and the audio thread needs the graph topology, this handoff will need a
// bigger snapshot (e.g. a swapped immutable graph), not just flags.
class CurrentAudioProcessor : public juce::AudioProcessor
{
public:
    CurrentAudioProcessor();
    ~CurrentAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Current"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return parameters; }

    // --- Canvas model (message thread only) ---------------------------------
    const std::vector<ModuleInstance>& modules() const { return moduleList; }
    int  addModule (ModuleType type, float x, float y);   // returns new id
    void moveModule (int id, float x, float y);
    void removeModule (int id);

    // Fires when the model is replaced wholesale behind the editor's back
    // (setStateInformation while the editor is open — project revert, preset
    // load). The canvas listens and rebuilds; its own edits don't need it
    // because it already knows what it changed.
    juce::ChangeBroadcaster canvasModelReplaced;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Publish which module types are present for the audio thread. Called on the
    // message thread after any change to moduleList; read lock-free (per-flag
    // atomics) in processBlock. Position never affects DSP in Phase 2, so only
    // presence is published.
    void refreshEngineConfig();

    juce::AudioProcessorValueTreeState parameters;

    std::vector<ModuleInstance> moduleList;
    int nextModuleId = 1;

    // Audio-thread-facing engine + its config snapshot.
    Engine engine;
    std::atomic<bool> engHasArp { false }, engHasRandom { false },
                      engHasQuantize { false }, engHasShift { false };

    // Cached parameter pointers (set in the ctor, read every block).
    std::atomic<float>* rootParam     = nullptr;
    std::atomic<float>* scaleParam    = nullptr;
    std::atomic<float>* quantizeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurrentAudioProcessor)
};
