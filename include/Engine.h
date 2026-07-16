#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cstdint>
#include <vector>

// The MIDI engine. There is no user wiring yet, so the modules run as a fixed
// implicit chain with baked-in default settings (the requirements' "the four
// modules run with fixed default settings that the user cannot change yet");
// the I/O modules are the first with a real, user-editable setting (their MIDI
// channel). Signal flow, per block:
//
//   host MIDI -> MIDI In (channel filter)
//     -> generators add notes   (Arp, Random)
//     -> modulators transform    (Quantize, then Shift)
//     -> Output (channel stamp) -> host
//
// Fixed defaults (deliberately not user-editable yet):
//   - Arp:      arpeggiates currently-held host notes, ascending, on a 1/16
//               grid, gate = half a step. Consumes the host notes (they are the
//               arp's input, so they don't also pass straight through).
//   - Random:   free-running random notes drawn from the global scale within
//               MIDI 48..72, 1/16 grid, gate = half a step.
//   - Quantize: snaps every note to the global root + scale.
//   - Shift:    transposes every note by +12 semitones.
//
// I/O module behaviour (channel editable per module):
//   - MIDI In:  filters which host events enter the graph by channel. With no
//               MIDI In on the canvas the implicit input accepts everything;
//               several MIDI Ins merge (union of their channels). Filtered
//               events are dropped entirely — they don't reach the arp either.
//   - Output:   stamps every event leaving the engine with its channel. With no
//               Output on the canvas channels pass through unchanged; several
//               Outputs duplicate the stream, one copy per channel (the
//               implicit chain's fan-out).
//
// Generators require the host transport to be playing; on stop, every note the
// engine generated is released (note-off) so nothing hangs — matching the
// requirements' transport-boundary rule. Host notes that passed through are
// remembered with the exact pitch/channel they were emitted at (activePass), so
// their note-offs always match the note-ons even if a module setting changed
// mid-note — the same no-hanging-notes invariant, extended to settings edits.
// With no modules on the canvas the MIDI passes through untouched.
//
// The engine is owned and driven entirely from the audio thread. The processor
// hands it a config snapshot (which module types are present + the I/O channel
// masks) published lock-free via atomics, plus the global root/scale/quantize
// each block.
class Engine
{
public:
    struct Config
    {
        bool hasArp      = false;
        bool hasRandom   = false;
        bool hasQuantize = false;
        bool hasShift    = false;
        bool hasMidiIn   = false;
        bool hasOutput   = false;

        // Bit (ch - 1) set = channel ch participates. inChannelMask is all-ones
        // when no MIDI In module narrows the input; outChannelMask is 0 when no
        // Output module is present (meaning: keep each event's own channel).
        std::uint16_t inChannelMask  = 0xffff;
        std::uint16_t outChannelMask = 0;

        bool anyModule() const
        {
            return hasArp || hasRandom || hasQuantize || hasShift
                || hasMidiIn || hasOutput;
        }
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

    // Emit note-offs for every note the engine is currently sounding (generated
    // and passed-through respectively).
    void flushGeneratedNotes (juce::MidiBuffer& midi, int sample);
    void flushPassedNotes (juce::MidiBuffer& midi, int sample);

    double sr = 44100.0;

    // Host notes currently held (raw incoming pitch), the Arp's input.
    std::array<bool, 128> held {};

    // Notes the engine generated that still need a note-off, with the remaining
    // sample count until release. Pitch/channel stored here are the already-
    // mapped (output) values, so the off matches the on.
    struct ActiveNote { int note; int channel; int samplesLeft; };
    std::vector<ActiveNote> activeGen;

    // Host notes that passed through, remembered as (incoming -> emitted) so the
    // incoming note-off can release exactly what was emitted, whatever the
    // config says by then. One entry per emitted copy (an Output fan-out emits
    // several per incoming note).
    struct PassNote { int inNote; int inChannel; int outNote; int outChannel; };
    std::vector<PassNote> activePass;

    // 1/16 step clock. Free-running sample counter reset on transport start.
    double samplesToNextStep = 0.0;
    int    arpIndex = 0;
    bool   wasPlaying = false;

    juce::Random rng;
};
