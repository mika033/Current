#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <atomic>
#include <memory>
#include "ModuleTypes.h"
#include "ModuleSettings.h"
#include "EngineGraph.h"
#include "Engine.h"
#include "AuditionSynth.h"

// Parameter ids for the global settings. Central so the editor's combos and the
// processor's APVTS layout can't drift.
namespace ParamIDs
{
    constexpr auto root  = "root";
    constexpr auto scale = "scale";
    constexpr auto theme = "theme";
}

// One placed module on the canvas. Position is stored in canvas coordinates
// (top-left of the node). `channel` is the I/O modules' one setting (see
// defaultChannelFor for its per-type semantics); `settings` is the shared
// settings blob used by every non-I/O type. Each type ignores the fields
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

// One cable on the canvas: the `from` module's output port wired into the
// `to` module's input port. Modules have at most one port of each direction,
// so the module ids identify the connection completely.
struct ModuleConnection
{
    int fromId = 0;
    int toId   = 0;
};

// The processor: a MIDI effect whose processBlock produces no audio and drives
// the Engine with the wired module graph. The canvas model (modules +
// connections) lives here (not in the editor) so it survives the editor being
// closed and reopened and is persisted in the DAW project state.
//
// Threading: the model is touched only from the message thread (editor +
// canvas). After every change, rebuildGraph() bakes it into an immutable
// GraphSnapshot (settings resolved to engine units, nodes topologically
// sorted) and publishes it under a SpinLock; processBlock try-locks to adopt
// the newest snapshot and otherwise keeps using its previous one, so the audio
// thread never blocks. This replaced the per-module-type atomic flags of the
// fixed-chain era once real wiring landed.
class CurrentAudioProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
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
    // Everywhere except the Standalone this is a MIDI effect. The Standalone
    // must NOT claim to be one: JUCE's AudioProcessorPlayer hands a MIDI
    // effect a zero-channel buffer (findMostSuitableLayout returns {0,0}) and
    // zeroes the device output itself, which would keep the audition synth
    // silent — audio out is the Standalone's whole point. LAM sidesteps this
    // by building its Standalone from an IS_SYNTH target; Current's single
    // target answers per-wrapper instead.
    bool isMidiEffect() const override { return wrapperType != wrapperType_Standalone; }
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

    // Bus layout depends on the wrapper: the Standalone drops the audio input
    // bus (see the definition for why), so it must be picked before the base
    // AudioProcessor ctor runs — hence a static helper fed into the init list.
    static BusesProperties makeBusesProperties();

    // True in every wrapper with an audio output bus (Standalone, VST3). False
    // only in the AU MIDI-FX, which sheds its buses for auval — the Settings
    // view hides the Audition Synth panel there.
    bool isAuditionSynthSupported() const { return auditionSynthSupported; }

    // --- Internal transport (Standalone / playhead-less hosts) --------------
    // The Standalone's playhead reports isPlaying == false forever (there is
    // no host transport), which would leave every stepped module silent; the
    // menu bar shows a Play toggle and a Tempo stepper instead, wired to
    // these (the LAM approach). processBlock synthesizes a playhead position
    // from them, so the engine never knows the difference.
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    void setStandalonePlay (bool on)   { standalonePlay.store (on, std::memory_order_release); }
    bool getStandalonePlay() const     { return standalonePlay.load (std::memory_order_acquire); }
    void   setInternalBpm (double bpm)
    {
        internalBpm.store (bpm, std::memory_order_relaxed);
        // Manual tempo is a user preference, kept across Standalone launches
        // (plugin-state persistence is off there). Called from the message
        // thread (the menu bar's stepper), so the file write is safe.
        if (standaloneProps != nullptr)
        {
            standaloneProps->setValue ("internalBpm", bpm);
            standaloneProps->saveIfNeeded();
        }
    }
    double getInternalBpm() const      { return internalBpm.load (std::memory_order_relaxed); }

    // --- Canvas model (message thread only) ---------------------------------
    const std::vector<ModuleInstance>& modules() const { return moduleList; }
    int  addModule (ModuleType type, float x, float y);   // returns new id
    void moveModule (int id, float x, float y);
    void removeModule (int id);                           // also drops its cables
    void setModuleChannel (int id, int channel);
    int  getModuleChannel (int id) const;
    void setModuleSettings (int id, const ModuleSettings& settings);
    ModuleSettings getModuleSettings (int id) const;

    // --- Connections (message thread only) ----------------------------------
    // A connection runs from `fromId`'s output port to `toId`'s input port.
    // canConnect is the single validity rule: both modules exist, the ports
    // exist (generators/MIDI In have no input, Output has no output), no
    // duplicate, no self-loop, and no cycle — the engine runs the graph in
    // topological order, so a feedback loop can never be published.
    const std::vector<ModuleConnection>& connections() const { return connectionList; }
    bool canConnect (int fromId, int toId) const;
    bool addConnection (int fromId, int toId);            // false if canConnect refuses
    void removeConnection (int fromId, int toId);

    // Fires when the model is replaced wholesale behind the editor's back
    // (setStateInformation while the editor is open — project revert, preset
    // load). The canvas listens and rebuilds; its own edits don't need it
    // because it already knows what it changed.
    juce::ChangeBroadcaster canvasModelReplaced;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Bake the canvas model into a fresh GraphSnapshot and publish it for the
    // audio thread. Called on the message thread after every model change.
    // Topology edits (modules/cables added or removed) bump topologyVersion
    // first, which tells the engine to cut sounding notes and reset per-node
    // state; settings edits republish under the same version so notes ring on.
    void rebuildGraph();

    const ModuleInstance* findModule (int id) const;
    ModuleParams paramsFor (const ModuleInstance& m) const;

    juce::AudioProcessorValueTreeState parameters;

    std::vector<ModuleInstance>   moduleList;
    std::vector<ModuleConnection> connectionList;
    int nextModuleId    = 1;
    int topologyVersion = 1;

    Engine engine;

    // The audition synth voices the outgoing MIDI right after the engine runs,
    // so the user hears exactly what the plugin emits. Voiceless (and never
    // prepared or processed) where the wrapper has no audio output bus.
    AuditionSynth auditionSynth;
    bool auditionSynthSupported = false;

    // The published snapshot and the audio thread's adopted copy. The lock
    // guards only the shared_ptr swap; the audio thread try-locks, so a
    // rebuild can never block processBlock (it just keeps last block's graph
    // one block longer). audioGraph is audio-thread-only; releasing a
    // superseded snapshot may deallocate on the audio thread, which this
    // codebase already accepts elsewhere (the engine's own buffers grow there).
    juce::SpinLock graphLock;
    std::shared_ptr<const GraphSnapshot> publishedGraph;
    std::shared_ptr<const GraphSnapshot> audioGraph;

    // Cached parameter pointers (set in the ctor, read every block).
    std::atomic<float>* rootParam  = nullptr;
    std::atomic<float>* scaleParam = nullptr;

    // Standalone-only preferences (theme, manual tempo). The Standalone
    // reports no plugin state (see getStateInformation), so these survive in
    // a small per-user file instead. Null in plugin wrappers.
    std::unique_ptr<juce::PropertiesFile> standaloneProps;

    // Standalone-only: persists the Theme parameter into standaloneProps.
    void parameterChanged (const juce::String& paramID, float newValue) override;

    // Internal-transport state (see the accessors above). The atomics are the
    // UI -> audio handoff for the menu bar's Play/Tempo controls; internalQn
    // and prevInternalPlay are audio-thread-only. Tempo is a runtime
    // preference, not patch content, so it is not an APVTS parameter; the
    // Standalone keeps it in standaloneProps.
    std::atomic<bool>   standalonePlay { false };
    std::atomic<double> internalBpm { 120.0 };
    double internalQn = 0.0;        // synthesized song position, quarter notes
    bool   prevInternalPlay = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurrentAudioProcessor)
};
