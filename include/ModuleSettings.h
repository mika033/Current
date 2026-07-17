#pragma once

#include <juce_core/juce_core.h>

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

// One module's settings. Random uses root/scale/rate/range; Scale uses
// root/scale/rate/repeat plus mode/octaves/endOnRoot; Arp uses
// mode/rate/octaves/gate/repeat. Other module types ignore the whole struct.
// Root/scale overrides of -1 mean "follow the global menu-bar setting" — the
// engine resolves them per block, so a module left on Global tracks later
// menu-bar changes.
struct ModuleSettings
{
    int rootOverride  = -1;   // -1 = Global, else 0..11 (C..B)
    int scaleOverride = -1;   // -1 = Global, else index into the global scale list
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
};
