#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <atomic>
#include "ModuleTypes.h"
#include "ModuleSettings.h"
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
// (top-left of the node). `channel` is the I/O modules' one setting (see
// defaultChannelFor for its per-type semantics); `settings` is the shared
// settings blob used by Random, Scale, and Arp. Each type ignores the fields
// that aren't its.
struct ModuleInstance
{
    int        id = 0;
    ModuleType type = ModuleType::Arp;
    float      x = 0.0f;
    float      y = 0.0f;
    int        channel = 0;
    ModuleSettings settings;
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
    // Non-empty name: Steinberg's VST3 validator rejects an unnamed program.
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return parameters; }

    // --- Canvas model (message thread only) ---------------------------------
    const std::vector<ModuleInstance>& modules() const { return moduleList; }
    int  addModule (ModuleType type, float x, float y);   // returns new id
    void moveModule (int id, float x, float y);
    void removeModule (int id);
    void setModuleChannel (int id, int channel);
    int  getModuleChannel (int id) const;
    void setModuleSettings (int id, const ModuleSettings& settings);
    ModuleSettings getModuleSettings (int id) const;

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

    // Audio-thread-facing engine + its config snapshot. The masks carry the I/O
    // modules' channel settings (see Engine::Config for their semantics); each
    // field is independently atomic, which is fine because a block that sees a
    // half-updated combination is indistinguishable from the edit landing one
    // block later.
    Engine engine;
    std::atomic<bool> engHasArp { false }, engHasRandom { false },
                      engHasScaleGen { false }, engHasLfo { false },
                      engHasQuantize { false }, engHasShift { false },
                      engHasMidiIn { false }, engHasOutput { false };
    std::atomic<std::uint16_t> engInChannelMask { 0xffff }, engOutChannelMask { 0 };

    // Module settings for the audio thread, from the first Arp / Random /
    // Scale module on the canvas (the implicit chain runs one of each; extra
    // copies share the first one's settings until wiring lands). Rates,
    // repeats, and gates are published as option-table indices; processBlock
    // converts to quarter notes / fractions. Root/scale of -1 = follow the
    // global parameter.
    std::atomic<int> engArpMode { ModuleOptions::kModeUp },
                     engArpRate { ModuleOptions::kRate1_16 },
                     engArpOctaves { 1 },
                     engArpGate { ModuleOptions::kGateHalf },
                     engArpRepeat { ModuleOptions::kRepeatEndless };
    std::atomic<int> engRandomRoot { -1 }, engRandomScale { -1 },
                     engRandomRate { ModuleOptions::kRate1_16 },
                     engRandomFrom { 24 }, engRandomTo { 48 };
    std::atomic<int> engScaleRoot { -1 }, engScaleScale { -1 },
                     engScaleRate { ModuleOptions::kRate1_8 },
                     engScaleRepeat { ModuleOptions::kRepeatOneBar },
                     engScaleOctaves { 1 },
                     engScaleMode { ModuleOptions::kModeUp };
    std::atomic<bool> engScaleEndOnRoot { true };
    std::atomic<int> engLfoRoot { -1 }, engLfoScale { -1 },
                     engLfoRate { ModuleOptions::kRate1_16 },
                     engLfoCycle { ModuleOptions::kLfoCycleOneBar },
                     engLfoShape { ModuleOptions::kLfoSine },
                     engLfoDepthOct { 1 }, engLfoDepthSteps { 0 },
                     engLfoPhase { 0 };
    std::atomic<int> engShiftAmount { 0 },
                     engShiftScale { ModuleOptions::kScaleGlobal };

    // Cached parameter pointers (set in the ctor, read every block).
    std::atomic<float>* rootParam     = nullptr;
    std::atomic<float>* scaleParam    = nullptr;
    std::atomic<float>* quantizeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurrentAudioProcessor)
};
