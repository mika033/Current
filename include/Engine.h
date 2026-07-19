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
//     -> pitch modulators transform (Scale, Progression, Shift, then Mirror)
//     -> Quantize re-times note-ons onto its swung grid
//     -> Output (channel stamp) -> Delay adds echoes
//     -> Strum fans simultaneous chord notes out over a short window
//     -> Humanize warps the final stream (swing/timing/velocity feel) -> host
//
// Stepped-module behaviour (each on its own step clock, gate = a fraction of
// its step — user-set on every note-emitting Rate module: Random, Scale, LFO,
// Arp; velocity 100; a root/scale override of -1 means "use the global value",
// a scale override of -2 (Off) means chromatic; a repeat window of <= 0 qn
// means Endless — the pattern never resets):
//   - Arp:      a modulator that re-times its input: arpeggiates currently-held
//               host notes on its step grid, walking them per its mode (Up,
//               Down, Up-Down, Random) across `arpOctaves` octaves. Consumes
//               the host notes (they are the arp's input, so they don't also
//               pass straight through). The walk position comes from the song
//               position itself, so the phrase is identical on every host
//               loop pass; every `arpRepeatQn` quarter notes the walk resets
//               to the pattern start (Endless by default).
//   - Random:   one random note per step, drawn uniformly from the pitches of
//               its (root, scale) that lie inside its inclusive note range.
//   - Scale:    walks its (root, scale) stepwise from the root at octave 3
//               (MIDI 48 + root), spanning `scaleOctaves` octaves, up or down
//               (down = the same notes reversed). endOnRoot appends the octave
//               root as a final step; otherwise the pattern ends on the
//               scale's last degree (the 7th in a 7-note scale). The pattern
//               restarts every `scaleRepeatQn` quarter notes counted from the
//               song's bar 0 (repeat choices assume 4/4): a pattern longer
//               than the window is cut off, a shorter one rests until the
//               window restarts. With repeat Endless it loops back-to-back —
//               the pattern restarts right after its last note.
//   - LFO:      a classic LFO sampled on the note grid and mapped to pitch.
//               Each step evaluates the shape at the current position inside
//               the cycle (cycle length in bars, anchored to the song's bar 0,
//               plus the start-phase offset) and maps the bipolar value to a scale
//               member around the centre note (the root at octave 3), swinging
//               ± its depth (whole octaves + extra scale steps, both counted
//               in scale degrees). The Random shape redraws a value per note
//               instead of tracking the cycle.
//   - Chord:    emits one diatonic chord — its (root, scale) stacked per
//               `chordType` on `chordDegree`, inverted per `chordInversion` —
//               on a long grid: a new chord starts every `chordPeriodQn`
//               (anchored to the song's bar 0) and sounds for
//               `chordLengthQn`; the rest of the period is silent. Length >=
//               period plays legato back-to-back (the gate is capped one
//               sample short of the period, like every stepped gate).
//   - Drone:    holds its voicing (root / root+5th / root+octave / triad, the
//               root at octave 3 + `droneOctave`) on the same period/length
//               model as the Chord, but re-triggers immediately whenever the
//               pitches it should be holding change mid-hold — a root/scale
//               edit, a voicing edit, or an upstream Progression step — with
//               the remainder of the hold time carried over. Because a drone
//               is continuous material rather than an event stream, it
//               deliberately bypasses Quantize (nothing to re-time; its
//               starts sit on bar boundaries already) and the Delay (echoing
//               a held pad adds blips, not echoes).
//
// Modulators:
//   - Quantize: re-times notes, not pitches: while the transport is playing,
//               every note-on heading out of the chain (pass-through and
//               generated alike) is deferred to the next point of the
//               module's rate grid, anchored to the song's bar 0. Swing
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
//               step per `progRateQn`, anchored to the song's bar 0, looping)
//               and transposes every passing note to the current step: degree
//               moves in scale members of its (root, scale), the octave adds
//               chromatic ±12s. Degree I / octave 0 is a strict no-op, like
//               an idle Shift. While the transport is stopped the first step
//               applies, so auditioning matches how playback will start.
//   - Shift:    transposes every note by `shiftAmount`. With a scale active
//               (shiftScale Global/-1 or a named scale index) the amount moves
//               in scale degrees — out-of-scale notes snap to the scale first;
//               with the scale Off (kScaleOff) it moves chromatic semitones.
//   - Mirror:   inverts every note around its centre note, then constrains the
//               result to a [low, high] register window. The inversion reflects
//               a note the same distance on the far side of the centre (an
//               interval up becomes that interval down); with a scale active it
//               reflects in scale degrees so the result stays in key, with the
//               scale Off it reflects in chromatic semitones. A centre of
//               kMirrorCenterOff skips the inversion (the module then only
//               windows). A result outside [low, high] is either dropped
//               (mirrorBounds = Limit) or folded once back across the nearest
//               boundary and clamped if it still overshoots (Mirror). A dropped
//               note is simply not emitted (mapPitch returns -1) and books no
//               note-off, so nothing hangs; note-ons and note-offs map alike, so
//               a surviving note always releases what it sounded.
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
//   - Strum:    a time modulator that spreads a chord's notes out over a short
//               window, like a strummed guitar. Note-ons arriving within a
//               small fixed detection window are grouped into one chord, sorted
//               per strumMode (Up = low->high downstroke, Down = high->low
//               upstroke, Up-Down alternates strokes on successive strums,
//               Random shuffles), and emitted one after another over
//               strumSpreadSec — the inter-note spacing shaped by strumCurve,
//               the velocity ramped by strumVelTilt, each note nudged by a
//               deterministic strumJitter. Delay-only, like every timing module
//               here (a real-time MIDI FX can't play a note before it arrived),
//               so the chord fans late; the detection window is the small fixed
//               latency this costs. Each note-off is delayed by the same amount
//               as its note-on, so lengths and ordering are preserved and
//               nothing hangs. With strumRepeatQn > 0 the held chord is
//               re-strummed every that many quarter notes (a bar-based comping
//               engine); pair it with Up-Down for alternating strokes. Maps no
//               pitch, so it has no root/scale. Follows the shared transport
//               rule: on a transport stop its buffered material is discarded and
//               sounding notes released.
//   - Humanize: a final-stage "performance feel" pass over the whole outgoing
//               stream (pass-through, generated, quantized, and delayed alike).
//               Two structured, grid-locked controls — swing (the same
//               pair-based model as Quantize, applied here as a continuous,
//               delay-only time-warp instead of a snap, so it nudges rather than
//               quantizes) and a constant lay-back (drag behind the beat) — plus
//               a periodic velocity accent (strong beats louder) and three
//               random amounts (timing / velocity / length jitter). All timing
//               moves are delays (a real-time MIDI FX can't pull a note earlier
//               than it arrived), so lay-back drags and jitter drags/lengthens;
//               the random draws are a deterministic function of song position,
//               so the humanized feel repeats identically on every loop pass.
//               Like Quantize it only warps while the transport plays (stopped,
//               notes pass straight through for immediate live feel); on stop or
//               removal its buffered note-offs are flushed so nothing hangs.
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
// Every grid above is derived from the host's ppq position each block (the
// half-open range [blockStart, blockEnd) owns its boundaries), so pressing
// play mid-bar lands the first step on the song's next real grid point, and
// host loop wraps and tempo changes can't put a pattern out of phase. A
// playhead without a ppq value falls back to counting from transport start.
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
        bool hasChord       = false;
        bool hasDrone       = false;
        bool hasQuantize    = false;
        bool hasScaleMod    = false;
        bool hasProgression = false;
        bool hasShift       = false;
        bool hasMirror      = false;
        bool hasDelay       = false;
        bool hasStrum       = false;
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

        // Random generator settings. Root/scale of -1 = use the global value;
        // scale of kScaleOff (-2) = draw chromatically. Gate is the note length
        // as a fraction of the step (like the Arp).
        int    randomRoot     = -1;
        int    randomScale    = -1;
        double randomStepQn   = 0.25;   // step length in quarter notes (1/16)
        double randomGateFrac = 0.5;
        int    randomFrom     = 24;     // inclusive MIDI note range
        int    randomTo       = 48;

        // Scale generator settings.
        int    scaleRoot      = -1;
        int    scaleScale     = -1;
        double scaleStepQn    = 0.5;    // step length in quarter notes (1/8)
        double scaleGateFrac  = 0.5;
        double scaleRepeatQn  = 4.0;    // pattern restarts every this many qn;
                                        // <= 0 = Endless (loops back-to-back)
        int    scaleOctaves   = 1;
        int    scaleMode      = 0;      // kModeUp or kModeDown (Up/Down only)
        bool   scaleEndOnRoot = true;

        // LFO generator settings. scale of kScaleOff (-2) = map chromatically.
        int    lfoRoot       = -1;
        int    lfoScale      = -1;
        double lfoStepQn     = 0.25;   // note grid in quarter notes (1/16)
        double lfoGateFrac   = 0.5;
        double lfoCycleQn    = 4.0;    // one full shape sweep (1 bar)
        int    lfoShape      = 0;      // ModuleOptions::kLfo* shape indices
        int    lfoDepthOct   = 1;      // swing around the centre: whole octaves
        int    lfoDepthSteps = 0;      //   ... plus extra scale steps
        double lfoPhase      = 0.0;    // start phase as a cycle fraction (0..1)

        // Chord generator settings. Period = how often a new chord starts,
        // length = how long it sounds within the period (both bar lengths).
        int    chordRoot      = -1;
        int    chordScale     = -1;
        int    chordDegree    = 0;     // 0..6 = I..VII
        int    chordType      = 0;     // index into ModuleOptions::chordTypeNames()
        int    chordInversion = 0;     // lowest N tones up an octave
        double chordLengthQn  = 4.0;   // 1 bar
        double chordPeriodQn  = 4.0;

        // Drone generator settings, same period/length model as the Chord.
        int    droneVoicing  = 0;      // ModuleOptions::kVoicing*
        int    droneRoot     = -1;
        int    droneScale    = -1;
        int    droneOctave   = 0;      // offset from the root-at-octave-3 centre
        double droneLengthQn = 16.0;   // 4 bars
        double dronePeriodQn = 16.0;

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
        // shiftRoot -1 = global root, >= 0 = named root (the degree walk's
        // reference); ignored when the scale is Off.
        int shiftAmount = 0;
        int shiftScale  = -1;
        int shiftRoot   = -1;

        // Mirror settings. Inverts pitch around mirrorCenter (kMirrorCenterOff
        // = -1 = Off, no inversion), then constrains to [mirrorLow, mirrorHigh].
        // mirrorScale/mirrorRoot follow the Shift model: the invert and the
        // boundary fold move in scale steps with a scale active, in chromatic
        // semitones when the scale is Off (-2). mirrorBounds: kMirrorLimit drops
        // an out-of-window note (mapPitch returns -1, so nothing is emitted and
        // no note-off is booked), kMirrorFold reflects it once across the nearest
        // edge then clamps.
        int mirrorCenter = 60;
        int mirrorLow    = 36;
        int mirrorHigh   = 84;
        int mirrorBounds = 0;   // ModuleOptions::kMirrorLimit / kMirrorFold
        int mirrorScale  = -1;
        int mirrorRoot   = -1;

        // Delay settings. delayScale/delayRoot follow the same model as Shift:
        // the per-echo shift moves in scale degrees with a scale active, in
        // chromatic semitones when the scale is Off (-2).
        double delayTimeQn   = 0.5;   // echo spacing in quarter notes (1/8)
        double delayFeedback = 0.5;   // per-echo velocity multiplier
        int    delayShift    = 0;     // per-echo pitch shift
        int    delayScale    = -1;
        int    delayRoot     = -1;

        // Strum settings. A time modulator that fans a chord's simultaneous
        // note-ons out over strumSpreadSec, delaying each successive note (a
        // real-time MIDI FX can only delay, never advance). strumMode is the
        // shared Direction (kMode*: Up = low->high, Down = high->low, Up-Down
        // alternates strokes on successive strums, Random shuffles); strumCurve
        // shapes the inter-note spacing (ModuleOptions::kStrumCurve*);
        // strumVelTilt (−1..+1) ramps velocity across the fan; strumJitter
        // (0..1) adds a deterministic per-note looseness. strumRepeatQn > 0
        // re-strums the held chord every that many quarter notes (a bar-based
        // comping engine); 0 = Endless (strum the chord once). Which note-ons
        // form one chord is decided by a small fixed detection window, not a
        // user control.
        double strumSpreadSec = 0.04;
        int    strumMode      = 0;      // ModuleOptions::kMode*
        int    strumCurve     = 0;      // ModuleOptions::kStrumCurve*
        double strumVelTilt   = 0.0;    // -1..+1 velocity ramp across the fan
        double strumJitter    = 0.0;    // 0..1 per-note random looseness
        double strumRepeatQn  = 0.0;    // <= 0 = Endless (strum once)

        // Humanize settings. A final-stage feel pass over the whole outgoing
        // stream (see Engine.h's flow note and Engine::process's post-pass).
        // humanizeStepQn is the groove grid swing and accent lock to; the five
        // amounts are 0..1 fractions (swingFraction of the 0..10 UI index).
        // Swing and layback are a pure, monotonic time-warp of every note event
        // (delay-only, so no host latency is needed); timing/velocity/length
        // jitter are deterministic per-note random, so the feel repeats on every
        // loop pass.
        bool   hasHumanize     = false;
        double humanizeStepQn  = 0.25;   // groove grid in quarter notes (1/16)
        double humanizeSwing   = 0.0;    // 0..1 pair-based swing (off-beats late)
        double humanizeLayback = 0.0;    // 0..1 constant drag behind the beat
        double humanizeAccent  = 0.0;    // 0..1 strong/weak velocity emphasis
        double humanizeTimeJit = 0.0;    // 0..1 random late offset per note-on
        double humanizeVelJit  = 0.0;    // 0..1 random velocity variation
        double humanizeLenJit  = 0.0;    // 0..1 random note lengthening

        // Bit (ch - 1) set = channel ch participates. inChannelMask is all-ones
        // when no MIDI In module narrows the input; outChannelMask is 0 when no
        // Output module is present (meaning: keep each event's own channel).
        std::uint16_t inChannelMask  = 0xffff;
        std::uint16_t outChannelMask = 0;

        bool anyModule() const
        {
            return hasArp || hasRandom || hasScaleGen || hasLfo || hasChord
                || hasDrone || hasQuantize || hasScaleMod || hasProgression
                || hasShift || hasMirror || hasDelay || hasStrum || hasHumanize
                || hasMidiIn || hasOutput;
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
    // Map a single pitch through the modulator chain (Scale, Progression at
    // `progIndex`, Shift, then Mirror). Applied to note-ons only — note-offs are
    // released from the activeGen/activePass bookkeeping, which remembers the
    // emitted pitch, so a progression step change mid-note can't hang anything.
    // Returns -1 when Mirror's Limit mode drops the note; callers must skip
    // emitting it (no note-off is booked, so nothing hangs).
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
    // mapped (output) values, so the off matches the on. `drone` marks the
    // Drone's held notes so it can find and re-trigger them mid-hold when its
    // harmony changes; everything else (gate countdown, transport-stop flush)
    // treats them like any generated note.
    struct ActiveNote { int note; int channel; int samplesLeft; bool drone = false; };
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

    // Humanize's final-stage time-warp buffers events whose humanized
    // (delay-only) time lands past this block; `samplesUntil` is block-relative
    // and decremented each block, exactly like EchoNote / QuantNote. Cleared /
    // flushed on transport stop or module removal (see the post-pass). Each held
    // note-on records the random timing jitter drawn for it (in quarter notes)
    // so its note-off can reuse the exact same jitter and the note's length is
    // preserved through the warp — the swing/layback part is a pure function of
    // song position and recomputed on both ends.
    struct HumanEvent { juce::MidiMessage msg; int samplesUntil; };
    std::vector<HumanEvent> pendingHuman;
    struct HumanHeld { int channel; int note; double jitterQn; };
    std::vector<HumanHeld> humanHeld;

    // Strum's grouping + fan-out state. Chord notes arriving within a small
    // detection window are collected into `strumGroup`, then finalized (sorted
    // by Direction, spread over the spread time). Because the fan order needs
    // the whole chord, the group's notes are withheld until the window closes,
    // which is the (small, fixed) latency Strum adds. All sample fields are
    // block-relative and aged by numSamples each block, exactly like the Delay/
    // Quantize/Humanize buffers, so nothing needs cross-block sample arithmetic.
    struct StrumInNote { int note; int channel; int velocity; int arrival; };
    struct StrumInOff  { juce::MidiMessage msg; int arrival; };
    struct StrumGroup
    {
        std::vector<StrumInNote> notes;   // note-ons collected so far
        std::vector<StrumInOff>  offs;    // offs for grouped notes released pre-finalize
        int  deadline = 0;                // block-relative; finalize when it lands this block
        bool open = false;
    };
    StrumGroup strumGroup;

    // Fanned note events whose scheduled time lands past this block (both
    // delayed note-ons and their length-matched note-offs). Aged like the other
    // pending buffers.
    struct StrumEvent { juce::MidiMessage msg; int samplesUntil; };
    std::vector<StrumEvent> pendingStrum;

    // Currently-sounding strummed notes: the delay applied to each note-on (so
    // its note-off can be delayed the same amount, preserving length and order)
    // plus its velocity (so the Repeat re-strum can re-strike the held chord).
    struct StrumHeld { int channel; int note; int velocity; int delay; };
    std::vector<StrumHeld> strumHeld;

    // Counts finalized strums so Up-Down can alternate its stroke and each
    // strum's jitter draws a distinct value.
    juce::int64 strumIndex = 0;

    // There are no step counters: every grid position (step clocks, repeat
    // windows, the LFO cycle, the Progression playhead, Quantize's swung
    // boundaries) is re-derived each block from the host's ppq position, so
    // nothing can drift across loop wraps or tempo changes. The only clock
    // state is this fallback for hosts whose playhead carries no ppq value:
    // quarter notes accumulated since the transport start, making such a host
    // behave as if the song began the moment play was pressed. (The processor
    // synthesizes a full position for the no-playhead cases, so this is a
    // last-resort path.)
    double fallbackQn = 0.0;
    bool   wasPlaying = false;

    juce::Random rng;
};
