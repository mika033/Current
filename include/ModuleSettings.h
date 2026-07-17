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
    // multiply. Most modules offer the whole list; the Scale generator's dialog
    // starts at 1/16 (index kScaleRateFirst).
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
    constexpr int kScaleRateFirst = kRate1_16;

    // Repeat: when a module's pattern resets and replays from the start,
    // counted from transport start. Endless (the normal default) means the
    // pattern just runs on until the transport stops. Quarter-note values
    // assume 4/4 (1 bar = 4 quarter notes) — good enough until the engine
    // reads the host time signature. Endless maps to 0 qn, which the engine
    // reads as "no repeat window".
    inline const juce::StringArray& repeatNames()
    {
        static const juce::StringArray names { "Endless", "1/4", "1/2", "1 bar",
                                               "2 bars", "3 bars", "4 bars" };
        return names;
    }

    inline double repeatQuarterNotes (int index)
    {
        static const double qn[] = { 0.0, 1.0, 2.0, 4.0, 8.0, 12.0, 16.0 };
        return qn[(size_t) juce::jlimit (0, 6, index)];
    }

    constexpr int kRepeatEndless = 0;
    constexpr int kRepeatOneBar  = 3;

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

    // Gate: how long each emitted note sounds, as a fraction of its step. Only
    // the Arp exposes it so far; the other stepped modules run the fixed 50%
    // default until they grow the control.
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

    // Scale-override sentinel shared with rootOverride/scaleOverride: -1 means
    // "follow the global menu-bar setting". Shift adds a second sentinel, Off,
    // meaning "no scale at all — work chromatically".
    constexpr int kScaleGlobal = -1;
    constexpr int kScaleOff    = -2;

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
    // LFO's cycle length and the Progression's rate — both are "how long one
    // musical unit lasts" choices, so they present the same list.
    inline const juce::StringArray& barLengthNames()
    {
        static const juce::StringArray names { "1/4 bar", "1/2 bar", "1 bar",
                                               "2 bars", "4 bars", "8 bars" };
        return names;
    }

    inline double barLengthQuarterNotes (int index)
    {
        static const double qn[] = { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
        return qn[(size_t) juce::jlimit (0, 5, index)];
    }

    constexpr int kBarsOneBar = 2;

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

// One module's settings. Random uses root/scale/rate/range; Scale (generator)
// uses root/scale/rate/repeat plus mode/octaves/endOnRoot; Arp uses
// mode/rate/octaves/gate/repeat; LFO uses root/scale/rate plus its lfo*
// fields; Quantize uses rate (its timing grid) and swing; the Scale modulator
// uses root/scale only; Progression uses root/scale plus its prog* fields;
// Shift uses scaleOverride (with the extra Off sentinel) and shiftAmount;
// Delay uses rate (its delay time) plus its delay* fields.
// Other module types ignore the whole struct. Root/scale
// overrides of -1 mean "follow the global menu-bar setting" — the engine
// resolves them per block, so a module left on Global tracks later menu-bar
// changes.
struct ModuleSettings
{
    int rootOverride  = -1;   // -1 = Global, else 0..11 (C..B)
    int scaleOverride = -1;   // -1 = Global, else index into the global scale
                              // list; Shift only: -2 (kScaleOff) = chromatic
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

    // Arp only (so far — see ModuleOptions::gateNames).
    int gate = ModuleOptions::kGateHalf;   // index into gateNames()

    // Shift only: transpose amount. Scale degrees when scaleOverride is Global
    // or a named scale, chromatic semitones when it is kScaleOff. 0 (the
    // default) passes notes through untouched until the user dials it in.
    int shiftAmount = 0;   // -kShiftRange..+kShiftRange

    // Quantize only: how far every second grid step is pushed late (the grid
    // itself is the shared `rate` field). Index into swingNames().
    int swing = ModuleOptions::kSwingOff;

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
    // to 1/8 at drop time). Each echo is shifted delayShift semitones from the
    // note before it, so a non-zero shift climbs (or falls) across the chain.
    int delayFeedback = ModuleOptions::kFeedbackHalf;   // index into feedbackNames()
    int delayShift    = 0;   // -kDelayShiftRange..+kDelayShiftRange semitones
};
