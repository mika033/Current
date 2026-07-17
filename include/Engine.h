#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cstdint>
#include <vector>

// The MIDI engine. There is no user wiring yet, so the modules run as a fixed
// implicit chain; the I/O modules, the Random/Scale generators, and the Arp
// carry real, user-editable settings, the rest still run baked-in defaults.
// Signal flow, per block:
//
//   host MIDI -> MIDI In (channel filter)
//     -> stepped modules add notes  (Arp, Random, Scale)
//     -> modulators transform       (Quantize, then Shift)
//     -> Output (channel stamp) -> host
//
// Stepped-module behaviour (each on its own step clock, gate = a fraction of
// its step — fixed at half for Random/Scale, the Arp's is user-set; velocity
// 100; a root/scale override of -1 means "use the global value"; a repeat
// window of <= 0 qn means Endless — the pattern never resets):
//   - Arp:      a modulator that re-times its input: arpeggiates currently-held
//               host notes on its step grid, walking them per its mode (Up,
//               Down, Up-Down, Random) across `arpOctaves` octaves. Consumes
//               the host notes (they are the arp's input, so they don't also
//               pass straight through). Every `arpRepeatQn` quarter notes the
//               walk resets to the pattern start (Endless by default).
//   - Random:   one random note per step, drawn uniformly from the pitches of
//               its (root, scale) that lie inside its inclusive note range.
//   - Scale:    walks its (root, scale) stepwise from the root at octave 3
//               (MIDI 48 + root), spanning `scaleOctaves` octaves, up or down
//               (down = the same notes reversed). endOnRoot appends the octave
//               root as a final step; otherwise the pattern ends on the
//               scale's last degree (the 7th in a 7-note scale). The pattern
//               restarts every `scaleRepeatQn` quarter notes counted from
//               transport start (repeat choices assume 4/4): a pattern longer
//               than the window is cut off, a shorter one rests until the
//               window restarts. With repeat Endless it loops back-to-back —
//               the pattern restarts right after its last note.
//
// Fixed defaults (deliberately not user-editable yet):
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
// Stepped modules require the host transport to be playing; on stop, every note
// the engine generated is released (note-off) so nothing hangs — matching the
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
        bool hasScaleGen = false;
        bool hasQuantize = false;
        bool hasShift    = false;
        bool hasMidiIn   = false;
        bool hasOutput   = false;

        // Arp settings. Until wiring lands the implicit chain runs one Arp at
        // most; extra copies on the canvas share the first one's settings (the
        // same one-instance rule applies to Random and Scale below).
        int    arpMode     = 0;      // ModuleOptions::kModeUp/Down/UpDown/Random
        double arpStepQn   = 0.25;   // step length in quarter notes (1/16)
        int    arpOctaves  = 1;
        double arpGateFrac = 0.5;    // note length as a fraction of the step
        double arpRepeatQn = 0.0;    // <= 0 = Endless (walk never resets)

        // Random generator settings. Root/scale of -1 = use the global value.
        int    randomRoot   = -1;
        int    randomScale  = -1;
        double randomStepQn = 0.25;   // step length in quarter notes (1/16)
        int    randomFrom   = 24;     // inclusive MIDI note range
        int    randomTo     = 48;

        // Scale generator settings.
        int    scaleRoot      = -1;
        int    scaleScale     = -1;
        double scaleStepQn    = 0.5;    // step length in quarter notes (1/8)
        double scaleRepeatQn  = 4.0;    // pattern restarts every this many qn;
                                        // <= 0 = Endless (loops back-to-back)
        int    scaleOctaves   = 1;
        int    scaleMode      = 0;      // kModeUp or kModeDown (Up/Down only)
        bool   scaleEndOnRoot = true;

        // Bit (ch - 1) set = channel ch participates. inChannelMask is all-ones
        // when no MIDI In module narrows the input; outChannelMask is 0 when no
        // Output module is present (meaning: keep each event's own channel).
        std::uint16_t inChannelMask  = 0xffff;
        std::uint16_t outChannelMask = 0;

        bool anyModule() const
        {
            return hasArp || hasRandom || hasScaleGen || hasQuantize
                || hasShift || hasMidiIn || hasOutput;
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

    // Per-module step clocks (samples until the next step lands), all reset
    // on transport start so every stepped module's first step fires at sample
    // 0. The rates differ per module now, so they can't share one counter.
    double arpSamplesToNext    = 0.0;
    double randomSamplesToNext = 0.0;
    double scaleSamplesToNext  = 0.0;
    int    arpIndex   = 0;   // position in the arp walk; advances only when a
                             // note fires, resets at each repeat-window start
    int    arpStep    = 0;   // arp grid steps since transport start — locates
                             // the repeat-window boundaries
    int    scaleStep  = 0;   // steps since transport start; % steps-per-repeat
                             // gives the position inside the repeat window
    bool   wasPlaying = false;

    juce::Random rng;
};
