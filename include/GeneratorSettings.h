#pragma once

#include <juce_core/juce_core.h>

// Per-module settings for the generator modules (Random and Scale), plus the
// option tables their dialogs and the engine share. GUI-free (juce_core only)
// so the processor/engine side can use it without pulling in components.
namespace GeneratorOptions
{
    // Step rates as note lengths. Values are in quarter notes so tempo math is
    // a single multiply. Random offers the whole list; the Scale generator's
    // dialog starts at 1/16 (index kScaleRateFirst).
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

    // Repeat intervals for the Scale generator: how often the pattern restarts.
    // Quarter-note values assume 4/4 (1 bar = 4 quarter notes) — good enough
    // until the engine reads the host time signature.
    inline const juce::StringArray& repeatNames()
    {
        static const juce::StringArray names { "1/4", "1/2", "1 bar", "2 bars", "3 bars", "4 bars" };
        return names;
    }

    inline double repeatQuarterNotes (int index)
    {
        static const double qn[] = { 1.0, 2.0, 4.0, 8.0, 12.0, 16.0 };
        return qn[(size_t) juce::jlimit (0, 5, index)];
    }

    constexpr int kRepeatOneBar = 2;

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

// One generator module's settings. Random uses root/scale/rate/range; Scale
// uses root/scale/rate plus the pattern fields. Other module types ignore the
// whole struct. Root/scale overrides of -1 mean "follow the global menu-bar
// setting" — the engine resolves them per block, so a module left on Global
// tracks later menu-bar changes.
struct GeneratorSettings
{
    int rootOverride  = -1;   // -1 = Global, else 0..11 (C..B)
    int scaleOverride = -1;   // -1 = Global, else index into the global scale list
    int rate = GeneratorOptions::kRate1_16;   // index into rateNames()

    // Random only: inclusive MIDI note range to draw from. Defaults are
    // recomputed from the effective root when the module is created (root at
    // octave 1 up to root at octave 3); these values are the C-root case.
    int rangeFrom = 24;   // C1
    int rangeTo   = 48;   // C3

    // Scale only.
    int  octaves   = 1;      // how many octaves the pattern spans (1..4)
    bool down      = false;  // false = up, true = down (same notes, reversed)
    bool endOnRoot = true;   // true = append the octave root as a final step,
                             // false = end on the scale's last degree (the 7th)
    int  repeat = GeneratorOptions::kRepeatOneBar;   // index into repeatNames()
};
