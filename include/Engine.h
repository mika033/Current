#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

// The Phase 2 MIDI engine. There is no user wiring yet, so the modules run as a
// fixed implicit chain with baked-in default settings (the requirements' "the
// four modules run with fixed default settings that the user cannot change
// yet"). Signal flow, per block:
//
//   host MIDI (implicit MIDI In)
//     -> generators add notes   (Arp, Random)
//     -> modulators transform    (Quantize, then Shift)
//     -> host output
//
// Fixed defaults (all deliberately not user-editable in Phase 2):
//   - Arp:      arpeggiates currently-held host notes, ascending, on a 1/16
//               grid, gate = half a step. Consumes the host notes (they are the
//               arp's input, so they don't also pass straight through).
//   - Random:   free-running random notes drawn from the global scale within
//               MIDI 48..72, 1/16 grid, gate = half a step.
//   - Quantize: snaps every note to the global root + scale.
//   - Shift:    transposes every note by +12 semitones.
//
// Generators require the host transport to be playing; on stop, every note the
// engine generated is released (note-off) so nothing hangs — matching the
// requirements' transport-boundary rule. With no modules on the canvas the MIDI
// passes through untouched.
//
// The engine is owned and driven entirely from the audio thread. The processor
// hands it a config snapshot (which module types are present) published lock-
// free via atomics, plus the global root/scale/quantize each block.
class Engine
{
public:
    struct Config
    {
        bool hasArp      = false;
        bool hasRandom   = false;
        bool hasQuantize = false;
        bool hasShift    = false;

        bool anyModule() const { return hasArp || hasRandom || hasQuantize || hasShift; }
    };

    void prepare (double sampleRate);
    void reset();

    // Transforms `midi` in place. `pos` may be null (no playhead).
    void process (juce::MidiBuffer& midi,
                  int numSamples,
                  const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                  int root, int scaleIndex, bool globalQuantize,
                  const Config& cfg);

private:
    // Map a single pitch through the modulator chain (Quantize then Shift). Same
    // pure function is applied to note-ons and note-offs so their pitches always
    // match and no note can hang.
    int mapPitch (int note, int root, int scaleIndex,
                  bool globalQuantize, const Config& cfg) const;

    // Emit note-offs for every note the engine is currently sounding.
    void flushGeneratedNotes (juce::MidiBuffer& midi, int sample);

    double sr = 44100.0;

    // Host notes currently held (raw incoming pitch), the Arp's input.
    std::array<bool, 128> held {};

    // Notes the engine generated that still need a note-off, with the remaining
    // sample count until release. Pitch stored here is the already-mapped
    // (output) pitch, so the off matches the on.
    struct ActiveNote { int note; int channel; int samplesLeft; };
    std::vector<ActiveNote> activeGen;

    // 1/16 step clock. Free-running sample counter reset on transport start.
    double samplesToNextStep = 0.0;
    int    arpIndex = 0;
    bool   wasPlaying = false;

    juce::Random rng;
};
