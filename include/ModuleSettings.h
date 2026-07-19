#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// The shared per-module settings blob and the option tables behind it. Several
// settings recur across modules (root/scale override, rate, repeat, mode,
// octaves, gate) — modules.md's "Shared settings" section is the product-level
// description. Defining the option lists once here means every module's dialog
// and the engine agree on the same values, so the user meets the identical
// control wherever it appears. GUI-free (juce_core only) so the
// processor/engine side can use it without pulling in components.
namespace ModuleOptions
{
    // Step rates as note lengths: the grid a generator emits on, or a modulator
    // re-times to. Values are in quarter notes so tempo math is a single
    // multiply. One canonical list (1/32..1/1) everywhere Rate appears — no
    // module trims it, so the user meets the identical Rate control wherever it
    // is (see the Rate/Length resolution in CLAUDE.md).
    inline const juce::StringArray& rateNames()
    {
        static const juce::StringArray names { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
        return names;
    }

    inline double rateQuarterNotes (int index)
    {
        static const double qn[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
        return qn[(size_t) juce::jlimit (0, 5, index)];
    }

    constexpr int kRate1_16 = 1;
    constexpr int kRate1_8  = 2;

    // Repeat: the period after which a module restarts from its start,
    // regardless of where it currently is — the single meaning shared by every
    // module that has a Repeat (Scale gen, Arp, Chord, Drone). Endless (the
    // normal default) = never restart; on a stepped module that means the
    // pattern runs on until the transport stops, on a Chord/Drone it means the
    // one chord/hold sounds once and is not re-triggered. Repeat is orthogonal
    // to Rate/Length: a Chord with Length 2 bars + Repeat 4 bars sounds 2 bars
    // then rests 2. One canonical list everywhere Repeat appears — Endless plus
    // bar lengths (the Length list's finite entries, extended to 8/16 bars for
    // long Drone holds). Quarter-note values assume 4/4; Endless maps to 0 qn,
    // which the engine reads as "no repeat window".
    inline const juce::StringArray& repeatNames()
    {
        static const juce::StringArray names { "Endless", "1/4 bar", "1/2 bar",
                                               "1 bar", "2 bars", "4 bars",
                                               "8 bars", "16 bars" };
        return names;
    }

    inline double repeatQuarterNotes (int index)
    {
        static const double qn[] = { 0.0, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
        return qn[(size_t) juce::jlimit (0, 7, index)];
    }

    constexpr int kRepeatEndless  = 0;
    constexpr int kRepeatOneBar   = 3;
    constexpr int kRepeatFourBars = 5;

    // Pattern direction. The Scale generator offers the first two (its walk is
    // a fixed pattern, so Up-Down/Random add nothing a longer pattern
    // wouldn't); the Arp offers all four.
    inline const juce::StringArray& modeNames()
    {
        static const juce::StringArray names { "Up", "Down", "Up-Down", "Random" };
        return names;
    }

    constexpr int kModeUp     = 0;
    constexpr int kModeDown   = 1;
    constexpr int kModeUpDown = 2;
    constexpr int kModeRandom = 3;
    constexpr int kScaleModeCount = 2;   // Scale generator's dialog: Up/Down only

    // Gate: how long each emitted note sounds, as a fraction of its step.
    // Ships on every note-emitting Rate module — the generators (Random, Scale
    // gen, LFO) and the Arp — since only a module that originates note
    // durations has something for a gate to act on (Quantize/Delay re-time or
    // echo existing notes, so they carry a Rate but no Gate).
    inline const juce::StringArray& gateNames()
    {
        static const juce::StringArray names { "25%", "50%", "75%", "100%" };
        return names;
    }

    inline double gateFraction (int index)
    {
        static const double frac[] = { 0.25, 0.5, 0.75, 1.0 };
        return frac[(size_t) juce::jlimit (0, 3, index)];
    }

    constexpr int kGateHalf = 1;

    // Scale-override sentinels shared by scaleOverride across every pitch
    // module: -1 = "follow the global menu-bar scale", -2 = Off = "no scale at
    // all — work chromatically". The pitch transformers (Scale mod,
    // Progression, Shift, Delay's per-echo shift) and the two generators whose
    // output can be un-scaled (Random draws chromatically, LFO maps
    // chromatically) offer Off; the scale-walking generators (Scale gen, Chord,
    // Drone) do not, since they need a scale to generate at all. rootOverride
    // uses only -1 (Global) and named roots — "no root" has no meaning.
    constexpr int kScaleGlobal = -1;
    constexpr int kScaleOff    = -2;

    // Index of the Chromatic scale in the global scale list (kScaleNames in
    // PluginProcessor.cpp / kScales in ScaleTables.h — keep in lock-step). The
    // engine resolves an Off scale override to this for the generators whose
    // "Off = chromatic" behaviour is simply the chromatic scale.
    constexpr int kChromaticScale = 8;

    // Swing (Quantize): how far every second step of the rate grid is pushed
    // late, following the shared pair-based model from the standards repo's
    // swing-timing.md (the same maths as Little Arp Monster): within each pair
    // of steps the even step stretches by (1 + swing/2) and the odd step
    // shrinks by (1 - swing/2), so pair starts always sit on the straight
    // grid. 0% = straight time; ~67% is the classic triplet shuffle.
    inline const juce::StringArray& swingNames()
    {
        static const juce::StringArray names { "0%", "10%", "20%", "30%", "40%",
                                               "50%", "60%", "70%", "80%", "90%",
                                               "100%" };
        return names;
    }

    inline double swingFraction (int index)
    {
        return 0.1 * juce::jlimit (0, 10, index);
    }

    constexpr int kSwingOff = 0;

    // Shift: transpose range, symmetric around 0. The same numeric range is
    // used whether the module shifts chromatic semitones (scale Off) or scale
    // degrees (scale Global / named).
    constexpr int kShiftRange = 36;

    // LFO shapes, phase 0 = the start of the cycle. All bipolar in [-1, +1];
    // Sine and Triangle start at 0 rising, Saw Up rises -1 to +1, Saw Down
    // falls +1 to -1, Square is +1 for the first half. Random redraws a value
    // every note (cycle length and phase don't apply to it).
    inline const juce::StringArray& lfoShapeNames()
    {
        static const juce::StringArray names { "Sine", "Triangle", "Saw Up",
                                               "Saw Down", "Square", "Random" };
        return names;
    }

    constexpr int kLfoSine     = 0;
    constexpr int kLfoTriangle = 1;
    constexpr int kLfoSawUp    = 2;
    constexpr int kLfoSawDown  = 3;
    constexpr int kLfoSquare   = 4;
    constexpr int kLfoRandom   = 5;

    // Bar lengths (assuming 4/4, like the repeat table above). Shared by the
    // LFO's cycle length, the Progression's rate, and the Chord/Drone
    // Length + Repeat pair — all are "how long one musical unit lasts"
    // choices, so they present the same list. 16 bars exists mainly for the
    // Drone's long holds but every user of the list gets it.
    inline const juce::StringArray& barLengthNames()
    {
        static const juce::StringArray names { "1/4 bar", "1/2 bar", "1 bar",
                                               "2 bars", "4 bars", "8 bars",
                                               "16 bars" };
        return names;
    }

    inline double barLengthQuarterNotes (int index)
    {
        static const double qn[] = { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
        return qn[(size_t) juce::jlimit (0, 6, index)];
    }

    constexpr int kBarsOneBar   = 2;
    constexpr int kBarsFourBars = 4;

    // Progression: scale degrees a step can sit on. Fixed at the seven-degree
    // vocabulary of the common scales; on shorter scales (Pentatonic) the
    // higher degrees simply walk further — VI in a pentatonic is its 6th
    // member counting on, i.e. the octave root.
    inline const juce::StringArray& degreeNames()
    {
        static const juce::StringArray names { "I", "II", "III", "IV", "V", "VI", "VII" };
        return names;
    }

    // Progression per-step octave range (-2..+2) and the cap on how many steps
    // a progression can hold. The cap keeps the settings dialog manageable and
    // bounds the lock-free engine snapshot (one atomic per step).
    constexpr int kProgOctaveRange = 2;
    constexpr int kMaxProgSteps    = 8;

    // Delay feedback: each echo's velocity as a share of the note before it.
    // The velocity decay is what ends the repeats — echoes below the audible
    // floor aren't scheduled — so feedback doubles as the repeat count.
    inline const juce::StringArray& feedbackNames()
    {
        static const juce::StringArray names { "10%", "20%", "30%", "40%", "50%",
                                               "60%", "70%", "80%", "90%" };
        return names;
    }

    inline double feedbackFraction (int index)
    {
        return 0.1 * (juce::jlimit (0, 8, index) + 1);
    }

    constexpr int kFeedbackHalf = 4;   // 50%

    // Delay per-echo pitch shift range, in semitones. Cumulative across the
    // feedback chain (echo k sits k * shift above the source), so a modest
    // range already spans the keyboard after a few repeats.
    constexpr int kDelayShiftRange = 12;

    // LFO start phase, in quarter-cycle steps (0 / 90 / 180 / 270 degrees).
    inline const juce::StringArray& lfoPhaseNames()
    {
        static const juce::StringArray names { "0", "90", "180", "270" };
        return names;
    }

    inline double lfoPhaseFraction (int index)
    {
        return 0.25 * juce::jlimit (0, 3, index);
    }

    // Chord types as stacks of scale degrees on the chord's root degree, so a
    // chord stays diatonic to its (root, scale) whatever degree it sits on
    // (in C major, "7th" on V is G-B-D-F). Sus chords replace the third with
    // the neighbouring degree; "5th" is the two-note power chord.
    inline const juce::StringArray& chordTypeNames()
    {
        static const juce::StringArray names { "Triad", "7th", "Sus2", "Sus4",
                                               "5th", "6th" };
        return names;
    }

    inline const std::vector<int>& chordTypeDegrees (int index)
    {
        static const std::vector<std::vector<int>> kTones = {
            { 0, 2, 4 },      // Triad
            { 0, 2, 4, 6 },   // 7th
            { 0, 1, 4 },      // Sus2
            { 0, 3, 4 },      // Sus4
            { 0, 4 },         // 5th
            { 0, 2, 4, 5 }    // 6th
        };
        return kTones[(size_t) juce::jlimit (0, (int) kTones.size() - 1, index)];
    }

    // Chord inversions: how many of the lowest chord tones move up an octave.
    inline const juce::StringArray& chordInversionNames()
    {
        static const juce::StringArray names { "Root", "1st", "2nd" };
        return names;
    }

    // Drone voicings. The 5th is the pitch a perfect fifth up snapped into the
    // scale (so e.g. Locrian holds its diminished fifth); the triad stacks
    // scale degrees like the Chord generator.
    inline const juce::StringArray& droneVoicingNames()
    {
        static const juce::StringArray names { "Root", "Root+5th", "Root+Octave",
                                               "Triad" };
        return names;
    }

    constexpr int kVoicingRoot       = 0;
    constexpr int kVoicingRootFifth  = 1;
    constexpr int kVoicingRootOctave = 2;
    constexpr int kVoicingTriad      = 3;

    // Drone octave offset around the shared generator centre (root at octave 3).
    constexpr int kDroneOctaveRange = 2;

    // Note name for a MIDI pitch. Octave convention: C3 = MIDI 48 (the one
    // modules.md and the requirements use), so octave = note/12 - 1.
    inline juce::String midiNoteName (int note)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        note = juce::jlimit (0, 127, note);
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }
}

// One progression step: which scale degree the passing material is moved to
// (0 = I, the no-op degree) and an extra whole-octave offset.
struct ProgressionStep
{
    int degree = 0;   // 0..6 = I..VII
    int octave = 0;   // -kProgOctaveRange..+kProgOctaveRange

    bool operator== (const ProgressionStep& o) const
    {
        return degree == o.degree && octave == o.octave;
    }
};

// One module's settings. Random uses root/scale/rate/gate/range; Scale
// (generator) uses root/scale/rate/gate/repeat plus mode/octaves/endOnRoot;
// Arp uses mode/rate/octaves/gate/repeat; LFO uses root/scale/rate/gate plus
// its lfo* fields; Quantize uses rate (its timing grid) and swing; the Scale
// modulator uses root/scale only; Progression uses root/scale plus its prog*
// fields; Shift uses root/scale (scaleOverride carries the extra Off sentinel)
// and shiftAmount; Delay uses root/scale (Off sentinel) plus rate and its
// delay* fields; Chord uses root/scale plus its chord* fields; Drone uses
// root/scale plus its drone* fields; Chord and Drone share holdLength/
// holdRepeat; Humanize uses rate (its groove grid) + swing + its humanize*
// fields (no pitch mapping). Other module types ignore the whole struct. Root/scale overrides
// of -1 mean "follow the global menu-bar setting" — the engine resolves them
// per block, so a module left on Global tracks later menu-bar changes.
struct ModuleSettings
{
    int rootOverride  = -1;   // -1 = Global, else 0..11 (C..B)
    int scaleOverride = -1;   // -1 = Global, else index into the global scale
                              // list; -2 (kScaleOff) = chromatic on the modules
                              // that offer Off (see kScaleOff's note above)
    int rate = ModuleOptions::kRate1_16;   // index into rateNames()

    // Repeat defaults to Endless per the shared-settings rule; the Scale
    // generator overrides to 1 bar at drop time (its deliberate default lineup).
    int repeat = ModuleOptions::kRepeatEndless;   // index into repeatNames()

    // Scale generator and Arp: pattern direction and octave span.
    int mode    = ModuleOptions::kModeUp;   // index into modeNames()
    int octaves = 1;                        // 1..4

    // Random only: inclusive MIDI note range to draw from. Defaults are
    // recomputed from the effective root when the module is created (root at
    // octave 1 up to root at octave 3); these values are the C-root case.
    int rangeFrom = 24;   // C1
    int rangeTo   = 48;   // C3

    // Scale generator only.
    bool endOnRoot = true;   // true = append the octave root as a final step,
                             // false = end on the scale's last degree (the 7th)

    // Every note-emitting Rate module (Random, Scale gen, LFO, Arp) — see
    // ModuleOptions::gateNames.
    int gate = ModuleOptions::kGateHalf;   // index into gateNames()

    // Shift only: transpose amount. Scale degrees when scaleOverride is Global
    // or a named scale, chromatic semitones when it is kScaleOff. 0 (the
    // default) passes notes through untouched until the user dials it in.
    // Shift's root/scale come from the shared rootOverride/scaleOverride.
    int shiftAmount = 0;   // -kShiftRange..+kShiftRange

    // Quantize only: how far every second grid step is pushed late (the grid
    // itself is the shared `rate` field). Index into swingNames(). Humanize
    // reuses this same field for its own swing amount (identical semantics and
    // range — see the Humanize fields below).
    int swing = ModuleOptions::kSwingOff;

    // Humanize only. A final-stage "performance feel" pass over the outgoing
    // stream. Its grid (what swing and accent lock to) is the shared `rate`
    // field; its swing amount is the shared `swing` field above. These five are
    // the rest of its controls, all 0..10 (= 0..100% via swingFraction, like
    // swing): a structured groove pair (layback = drag behind the beat, accent
    // = periodic velocity emphasis on strong beats) and a random-touch trio
    // (timing / velocity / length jitter). See modules.md's Humanize entry.
    int humanizeLayback = 0;
    int humanizeAccent  = 0;
    int humanizeTimeJit = 0;
    int humanizeVelJit  = 0;
    int humanizeLenJit  = 0;

    // Progression only. The rate is a bar length (one step of the progression),
    // deliberately not the shared note-length rate — progressions move in bars,
    // not subdivisions. The step list is what the user edits in the dialog;
    // never empty (a fresh module holds one step on I).
    int progRate = ModuleOptions::kBarsOneBar;   // index into barLengthNames()
    std::vector<ProgressionStep> progSteps { {} };

    // LFO only. Depth is the swing around the centre note (the root at octave
    // 3), split into whole octaves plus extra scale steps — both directions,
    // so depth 1 octave means the pitch sweeps centre ± one octave.
    int lfoShape      = ModuleOptions::kLfoSine;    // index into lfoShapeNames()
    int lfoCycle      = ModuleOptions::kBarsOneBar; // index into barLengthNames()
    int lfoDepthOct   = 1;   // 0..4 octaves
    int lfoDepthSteps = 0;   // 0..6 extra scale steps
    int lfoPhase      = 0;   // index into lfoPhaseNames() (quarter-cycle steps)

    // Delay only. The delay time itself is the shared `rate` field (defaulted
    // to 1/8 at drop time). Each echo is shifted delayShift from the note
    // before it, so a non-zero shift climbs (or falls) across the chain — in
    // chromatic semitones when scaleOverride is Off, in scale degrees when a
    // scale is active (Global/named), mirroring Shift. Delay's root/scale come
    // from the shared rootOverride/scaleOverride.
    int delayFeedback = ModuleOptions::kFeedbackHalf;   // index into feedbackNames()
    int delayShift    = 0;   // -kDelayShiftRange..+kDelayShiftRange

    // Chord only: the chord to emit, as a scale degree of its (root, scale)
    // plus a diatonic stacking and an inversion.
    int chordDegree    = 0;   // 0..6 = I..VII
    int chordType      = 0;   // index into chordTypeNames()
    int chordInversion = 0;   // index into chordInversionNames()

    // Drone only: which pitches it holds and which octave the root sits in
    // (offset from the shared generator centre, root at octave 3).
    int droneVoicing = ModuleOptions::kVoicingRoot;
    int droneOctave  = 0;   // -kDroneOctaveRange..+kDroneOctaveRange

    // Chord and Drone: Repeat is how often a new chord/note starts (counted
    // from the song's bar 0) and Length is how long it sounds inside that
    // window — the rest of the window is silent. Length >= Repeat plays legato
    // back-to-back. holdLength indexes barLengthNames() (a Length is always
    // finite); holdRepeat indexes repeatNames() (the shared Repeat list, so it
    // can be Endless = sound once, never re-trigger). The Drone's drop-time
    // default raises both to 4 bars.
    int holdLength = ModuleOptions::kBarsOneBar;
    int holdRepeat = ModuleOptions::kRepeatOneBar;
};
