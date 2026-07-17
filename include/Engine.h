#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cstdint>
#include <vector>

// The MIDI engine. There is no user wiring yet, so the modules run as a fixed
// implicit chain; every module carries real, user-editable settings. Signal
// flow, per block:
//
//   host MIDI -> MIDI In (channel filter)
//     -> stepped modules add notes  (Arp, Random, Scale, LFO)
//     -> pitch modulators transform (Scale, then Progression, then Shift)
//     -> Quantize re-times note-ons onto its swung grid
//     -> Output (channel stamp) -> Delay adds echoes -> host
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
//   - LFO:      a classic LFO sampled on the note grid and mapped to pitch.
//               Each step evaluates the shape at the current position inside
//               the cycle (cycle length in bars from transport start, plus the
//               start-phase offset) and maps the bipolar value to a scale
//               member around the centre note (the root at octave 3), swinging
//               ± its depth (whole octaves + extra scale steps, both counted
//               in scale degrees). The Random shape redraws a value per note
//               instead of tracking the cycle.
//
// Modulators:
//   - Quantize: re-times notes, not pitches: while the transport is playing,
//               every note-on heading out of the chain (pass-through and
//               generated alike) is deferred to the next point of the
//               module's rate grid, counted from transport start. Swing
//               pushes every second grid point late by swing/2 of a step —
//               the shared pair-based model from swing-timing.md (pair
//               starts stay on the straight grid, so a generator running at
//               the same rate lands the classic shuffle). A held host note
//               released while its on is still waiting keeps its duration:
//               the off is deferred by the same amount. Deferred notes are
//               a buffer, so the shared transport rules apply — on stop the
//               queue is discarded; when the transport is stopped the module
//               passes everything through untouched (no grid to quantize to).
//   - Scale:    snaps every note's pitch to its (root, scale), each defaulting
//               to the global (-1). In-scale pitches pass untouched.
//   - Progression: walks its step list (scale degree I..VII + octave, one
//               step per `progRateQn`, counted from transport start, looping)
//               and transposes every passing note to the current step: degree
//               moves in scale members of its (root, scale), the octave adds
//               chromatic ±12s. Degree I / octave 0 is a strict no-op, like
//               an idle Shift. While the transport is stopped the first step
//               applies, so auditioning matches how playback will start.
//   - Shift:    transposes every note by `shiftAmount`. With a scale active
//               (shiftScale Global/-1 or a named scale index) the amount moves
//               in scale degrees — out-of-scale notes snap to the scale first;
//               with the scale Off (kScaleOff) it moves chromatic semitones.
//   - Delay:    every note-on leaving the chain (pass-through and generated
//               alike) schedules an echo one delay time (`delayTimeQn`) later
//               at `delayFeedback` times its velocity, which in turn schedules
//               the next, until the velocity decays below the audible floor —
//               feedback is thus also the repeat count. Each echo is shifted
//               `delayShift` semitones from the note before it (cumulative
//               across the chain); an echo that would leave the MIDI range
//               ends the chain instead of clamping. Echoes derive from the
//               final emitted stream, so they are not re-mapped through
//               Quantize/Shift, and their gate is half the delay time. On
//               transport stop pending echoes are discarded (and sounding
//               ones released) per the shared transport rules; echoes keep
//               running while the transport is stopped for live playing.
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
        bool hasArp         = false;
        bool hasRandom      = false;
        bool hasScaleGen    = false;
        bool hasLfo         = false;
        bool hasQuantize    = false;
        bool hasScaleMod    = false;
        bool hasProgression = false;
        bool hasShift       = false;
        bool hasDelay       = false;
        bool hasMidiIn      = false;
        bool hasOutput      = false;

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

        // LFO generator settings.
        int    lfoRoot       = -1;
        int    lfoScale      = -1;
        double lfoStepQn     = 0.25;   // note grid in quarter notes (1/16)
        double lfoCycleQn    = 4.0;    // one full shape sweep (1 bar)
        int    lfoShape      = 0;      // ModuleOptions::kLfo* shape indices
        int    lfoDepthOct   = 1;      // swing around the centre: whole octaves
        int    lfoDepthSteps = 0;      //   ... plus extra scale steps
        double lfoPhase      = 0.0;    // start phase as a cycle fraction (0..1)

        // Quantize settings: the timing grid and the swing amount (0..1, the
        // fraction of a step pair redistributed to the even step).
        double quantStepQn = 0.25;   // grid in quarter notes (1/16)
        double quantSwing  = 0.0;    // 0 = straight

        // Scale modulator settings. -1 = use the global value.
        int scaleModRoot  = -1;
        int scaleModScale = -1;

        // Progression settings. Steps are published as parallel fixed arrays
        // so the snapshot stays lock-free-friendly; only the first
        // `progStepCount` entries are meaningful.
        int    progRoot      = -1;
        int    progScale     = -1;
        double progRateQn    = 4.0;   // one progression step (1 bar)
        int    progStepCount = 0;
        std::array<int, 8> progDegrees {};   // 0..6 = I..VII
        std::array<int, 8> progOctaves {};   // -2..+2

        // Shift settings. shiftScale -1 = global scale (shift in degrees),
        // >= 0 = named scale (degrees), kScaleOff (-2) = chromatic semitones.
        int shiftAmount = 0;
        int shiftScale  = -1;

        // Delay settings.
        double delayTimeQn   = 0.5;   // echo spacing in quarter notes (1/8)
        double delayFeedback = 0.5;   // per-echo velocity multiplier
        int    delayShift    = 0;     // per-echo pitch shift, semitones

        // Bit (ch - 1) set = channel ch participates. inChannelMask is all-ones
        // when no MIDI In module narrows the input; outChannelMask is 0 when no
        // Output module is present (meaning: keep each event's own channel).
        std::uint16_t inChannelMask  = 0xffff;
        std::uint16_t outChannelMask = 0;

        bool anyModule() const
        {
            return hasArp || hasRandom || hasScaleGen || hasLfo || hasQuantize
                || hasScaleMod || hasProgression
                || hasShift || hasDelay || hasMidiIn || hasOutput;
        }
    };

    void prepare (double sampleRate);
    void reset();

    // Transforms `midi` in place. `pos` may be null (no playhead).
    void process (juce::MidiBuffer& midi,
                  int numSamples,
                  const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos,
                  int root, int scaleIndex,
                  const Config& cfg);

private:
    // Map a single pitch through the modulator chain (Scale, then Progression
    // at `progIndex`, then Shift). Applied to note-ons only — note-offs are
    // released from the activeGen/activePass bookkeeping, which remembers the
    // emitted pitch, so a progression step change mid-note can't hang anything.
    int mapPitch (int note, int root, int scaleIndex,
                  int progIndex, const Config& cfg) const;

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

    // Echoes the Delay has scheduled but not yet sounded. `samplesUntil` is
    // relative to the current block's start and is decremented as blocks pass;
    // once an echo fires it moves into activeGen for its gate-timed release
    // and schedules its successor. Cleared on transport stop (the shared
    // "buffered material is discarded" rule) — sample-based counting means a
    // host loop wrap simply spills echoes into the next pass, as speced.
    struct EchoNote { int note; int channel; int velocity; int samplesUntil; };
    std::vector<EchoNote> pendingEchoes;

    // Notes the Quantize module is holding back until their grid point.
    // `samplesUntil` (the emission point) and `arrivalOffset` (when the note
    // actually arrived) are block-relative like EchoNote and decremented
    // together, so arrivalOffset can go negative once the arrival block has
    // passed. gateSamples >= 0 = release is ours (generated notes, and host
    // notes whose off already arrived — the off keeps the played duration);
    // -1 = the host still holds the note, so firing registers it in
    // activePass and the host's note-off releases it. Cleared on transport
    // stop, like pendingEchoes.
    struct QuantNote
    {
        int note; int channel; int velocity;
        int samplesUntil; int arrivalOffset; int gateSamples;
        int inNote; int inChannel; bool fromHost;
    };
    std::vector<QuantNote> pendingQuant;

    // Per-module step clocks (samples until the next step lands), all reset
    // on transport start so every stepped module's first step fires at sample
    // 0. The rates differ per module now, so they can't share one counter.
    double arpSamplesToNext    = 0.0;
    double randomSamplesToNext = 0.0;
    double scaleSamplesToNext  = 0.0;
    double lfoSamplesToNext    = 0.0;
    int    arpIndex   = 0;   // position in the arp walk; advances only when a
                             // note fires, resets at each repeat-window start
    int    arpStep    = 0;   // arp grid steps since transport start — locates
                             // the repeat-window boundaries
    int    scaleStep  = 0;   // steps since transport start; % steps-per-repeat
                             // gives the position inside the repeat window
    int    lfoStep    = 0;   // steps since transport start — locates the
                             // position inside the LFO cycle
    // Quantize's grid bookkeeping: sample distance to the next straight grid
    // boundary and that boundary's index since transport start (index parity
    // decides whether swing delays it). Unlike the step clocks above these
    // advance every block, whether or not anything fired.
    double quantSamplesToNext = 0.0;
    int    quantStep          = 0;
    // Progression playhead in quarter notes since transport start.
    double progQn     = 0.0;
    bool   wasPlaying = false;

    juce::Random rng;
};
