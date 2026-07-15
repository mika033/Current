#pragma once

#include <array>
#include <vector>
#include <juce_core/juce_core.h>

// Scale interval tables + pitch helpers used by the Phase 1 engine (the Quantize
// modulator and the Random generator). The interval sets are semitone offsets
// within one octave from the root. Index order matches kScaleNames in
// PluginProcessor.cpp — keep the two in lock-step.
namespace ScaleTables
{
    inline const std::vector<int>& intervalsForScale (int scaleIndex)
    {
        static const std::vector<std::vector<int>> kScales = {
            { 0, 2, 4, 5, 7, 9, 11 },   // Major
            { 0, 2, 3, 5, 7, 8, 10 },   // Minor (natural)
            { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
            { 0, 1, 3, 5, 7, 8, 10 },   // Phrygian
            { 0, 2, 4, 6, 7, 9, 11 },   // Lydian
            { 0, 2, 4, 5, 7, 9, 10 },   // Mixolydian
            { 0, 1, 3, 5, 6, 8, 10 },   // Locrian
            { 0, 2, 4, 7, 9 },          // Pentatonic (major)
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } // Chromatic
        };
        return kScales[(size_t) juce::jlimit (0, (int) kScales.size() - 1, scaleIndex)];
    }

    // Is `note` a member of the (root, scale) set?
    inline bool isInScale (int note, int root, int scaleIndex)
    {
        const int pc = ((note - root) % 12 + 12) % 12;
        for (int iv : intervalsForScale (scaleIndex))
            if (iv == pc)
                return true;
        return false;
    }

    // Snap `note` to the nearest pitch in (root, scale). Ties round down. The
    // result stays within [0, 127].
    inline int snapToScale (int note, int root, int scaleIndex)
    {
        if (isInScale (note, root, scaleIndex))
            return note;

        for (int dist = 1; dist <= 6; ++dist)
        {
            if (note - dist >= 0   && isInScale (note - dist, root, scaleIndex)) return note - dist;
            if (note + dist <= 127 && isInScale (note + dist, root, scaleIndex)) return note + dist;
        }
        return note;   // unreachable for any real scale, but keeps the note valid
    }

    // The `degreeIndex`-th scale note at or above `minNote` (used by the Random
    // generator to pick in-scale pitches across a range).
    inline int scaleNoteAtOrAbove (int minNote, int degreeIndex, int root, int scaleIndex)
    {
        const auto& iv = intervalsForScale (scaleIndex);
        const int perOctave = (int) iv.size();
        const int octave = degreeIndex / perOctave;
        const int step   = degreeIndex % perOctave;

        // Lowest root at or below minNote, then walk up by octaves + degree.
        int base = root;
        while (base + 12 <= minNote) base += 12;
        while (base > minNote)       base -= 12;

        return juce::jlimit (0, 127, base + 12 * octave + iv[(size_t) step]);
    }
}
