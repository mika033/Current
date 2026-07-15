#pragma once

#include <juce_graphics/juce_graphics.h>

// Plain-data scheme per SnorkelAudioStandards design/themes.md §1.5 - no
// methods, no inheritance, raw colour literals live only here.
struct ColourScheme
{
    bool useDarkBase;

    juce::Colour windowBg;
    juce::Colour sectionBoxBg;
    juce::Colour panelBorder;
    juce::Colour widgetBg;
    juce::Colour widgetOutline;
    juce::Colour accent;
    juce::Colour text;

    // Plugin-specific extension fields (themes.md §2.1): canvas module
    // family colours. Generators/modulators don't have a finished family
    // taxonomy yet (see requirements doc "Open items > UI > Visual
    // encoding"), so Phase 1 gives generators and modulators one colour
    // each rather than inventing a taxonomy that would just be re-decided.
    juce::Colour generatorFill;
    juce::Colour modulatorFill;
};

inline const ColourScheme kLight {
    false,
    juce::Colour (0xffffffff),
    juce::Colour (0xffffffff),
    juce::Colour (0xffa8b8d4),
    juce::Colour (0xffffffff),
    juce::Colour (0xff7090b8),
    juce::Colour (0xff2070d0),
    juce::Colour (0xff1f3a5c),
    juce::Colour (0xff2070d0),
    juce::Colour (0xffc0392b),
};

inline const ColourScheme kDark {
    true,
    juce::Colour (0xff323e44),
    juce::Colour (0xff202036),
    juce::Colour (0xff404060),
    juce::Colour (0xff3a3a5c),
    juce::Colour (0xff555577),
    juce::Colour (0xff00d4ff),
    juce::Colour (0xffc8c8d8),
    juce::Colour (0xff00d4ff),
    juce::Colour (0xffe0685c),
};

// byIndex per themes.md §1.7: 0 -> Light, 1 -> Dark, so a zero-initialised
// choice resolves to the visual default.
inline const ColourScheme& byIndex (int index)
{
    return index == 1 ? kDark : kLight;
}

// Mutable file-scope pointer per themes.md §1.6, flipped by setActive().
// Painting code calls active() every frame rather than caching values.
inline const ColourScheme* currentScheme = &kLight;

inline const ColourScheme& active()
{
    return *currentScheme;
}

inline void setActive (const ColourScheme& scheme)
{
    currentScheme = &scheme;
}
