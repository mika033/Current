#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

// The audition synth: a small built-in subtractive pluck voice that plays the
// plugin's outgoing MIDI so the user hears a patch without routing to an
// instrument. Ported from Little Arp Monster (where it lives inline in the
// processor); the voice architecture, parameter set, FX chain, and defaults are
// kept identical so the two products sound the same.
//
// The plugin's production job is still MIDI-only — this is a monitoring
// convenience, gated per-block on the enabled parameter and only audible in
// wrappers that have an audio output bus (Standalone, VST3; never the AU
// MIDI-FX, which sheds its buses for auval).
//
// Signal chain per block: 16 pluck voices (supersaw + sub-octave sine through a
// resonant low-pass with its own fast envelope) -> tempo-synced feedback delay
// -> reverb, with one "Space" dial driving both effect mixes. The enable toggle
// is declicked with a short raised-cosine fade.
class AuditionSynth
{
public:
    AuditionSynth() = default;

    // Parameter ids — same strings as LAM so the dials line up conceptually
    // across products. Registered by addParameters, consumed by the Settings
    // view's attachments.
    static constexpr const char* cutoffId    = "synthcutoff";     // Hz (skewed)
    static constexpr const char* characterId = "synthcharacter";  // 0..1
    static constexpr const char* envAmtId    = "synthenvamt";     // 0..1
    static constexpr const char* decayId     = "synthampdecay";   // seconds
    static constexpr const char* spaceId     = "synthspace";      // 0..1 combined delay+reverb
    static constexpr const char* enabledId   = "synthenabled";    // bool

    /** Add the six synth parameters to the processor's layout. Ranges and
     *  defaults match LAM (cutoff 80..18k default 920 Hz, decay 0.05..2 s
     *  default 0.15 s, the 0..1 dials default 0.3/0.4/0.3). The enabled
     *  layout default is OFF — the DAW boot state; the Standalone seeds it ON
     *  at runtime (the layout can't tell wrapper types apart). */
    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    /** Cache the dial parameter pointers and, when the wrapper can be heard,
     *  build the 16 voices. With createVoices false the synth stays empty and
     *  process() is a no-op — the AU MIDI-FX case. */
    void attach (juce::AudioProcessorValueTreeState& state, bool createVoices);

    /** (Re)pin the sample rate, size the FX buffers, and reset all sounding
     *  state — call from prepareToPlay. maxBlockSize sizes the FX-send scratch
     *  buffer so process() never allocates on the audio thread. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Voice `midi` (the plugin's outgoing buffer for this block) into
     *  `buffer`, then run the delay->reverb chain and the toggle declick.
     *  Reads the enabled parameter itself so a toggle-off still fades the tail
     *  out instead of chopping it. `bpm` tempo-syncs the delay to a 1/8 note. */
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                  int numSamples, double bpm);

private:
    juce::Synthesiser synth;
    bool   voicesCreated = false;
    double sampleRate    = 44100.0;

    // Cached APVTS pointers (set in attach, read every block).
    std::atomic<float>* enabledParam = nullptr;
    std::atomic<float>* spaceParam   = nullptr;

    // MIDI-CC modulation state. process() scans the outgoing MIDI buffer for
    // four CCs, storing the last seen value as a 0..1 normalized float
    // (= cc / 127, 0.5 = CC 64 = neutral). The voices read these at block /
    // note cadence and modulate the matching dial:
    //   CC 74 (Brightness)      -> Cutoff   (log ±2 octaves)
    //   CC 75 (Decay Time)      -> Decay    (log ±2 octaves)
    //   CC 70 (Sound Variation) -> Character (additive, normalized)
    //   CC 71 (Resonance)       -> Env Amount (additive; no separate Q dial —
    //                              resonance rides the Character composite)
    std::atomic<float> cutoffMod    { 0.5f };
    std::atomic<float> decayMod     { 0.5f };
    std::atomic<float> characterMod { 0.5f };
    std::atomic<float> envAmtMod    { 0.5f };

    // In LAM this flips between a zero-sustain pluck (arp running) and a
    // sustaining shape (held-chord passthrough). Current has no such global
    // mode — note lengths here are user-authored content (module Gate and
    // Length settings, the Drone's multi-bar holds), so the voice must honour
    // note-offs rather than decay to silence on its own: the sustaining shape
    // is pinned on. The pluck transient (the filter-envelope smack) is
    // unaffected.
    std::atomic<float> sustainHeld  { 1.0f };

    // Declick ramp phase for the enabled toggle (0..1, advanced ~50 ms per
    // edge, mapped through a raised cosine in process()). Audio-thread owned.
    float fadeGain = 0.0f;

    // Insert FX. Reverb is a fixed "medium room" (one mix dial can't dial in a
    // cathedral or a tight ambience, so one useful spot is picked); the delay
    // is a per-channel feedback line tempo-synced to a 1/8 note per block. The
    // send path is high-passed at ~200 Hz so the wet tail doesn't pile up
    // sub-frequency energy from the voice's sub oscillator.
    juce::Reverb       reverb;
    std::vector<float> delayLineL, delayLineR;
    int                delayWriteIndex = 0;
    float fxHpfAlpha    = 0.0f;
    float fxHpfPrevInL  = 0.0f;
    float fxHpfPrevOutL = 0.0f;
    float fxHpfPrevInR  = 0.0f;
    float fxHpfPrevOutR = 0.0f;
    // Sized once in prepare() and reused — allocating per block on the audio
    // thread is forbidden in RT code.
    juce::AudioBuffer<float> fxSendBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuditionSynth)
};
