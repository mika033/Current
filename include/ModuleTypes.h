#pragma once

#include <juce_graphics/juce_graphics.h>
#include "Theme.h"

// The catalogue of module types the canvas knows about. Phase 2 shipped two
// generators and two modulators; the two I/O modules (MIDI In / Output)
// joined next. New modules slot in by extending this enum and kCatalogue;
// nothing else in the canvas needs to change.

enum class ModuleKind
{
    Generator,   // drawn as a square (has outputs only, once ports land)
    Modulator,   // drawn as a circle
    IO           // drawn as a triangle: MIDI In points right, Output left
};

enum class ModuleType
{
    // Generators
    Random,
    ScaleGen,   // the "Scale" module — suffixed to avoid reading as a scale type
    Lfo,
    Chord,
    Drone,
    // Modulators (Arp transforms held input notes into an arpeggio, so it is a
    // modulator, not a generator)
    Arp,
    Quantize,
    ScaleMod,      // the "Scale" modulator — forces passing notes onto a scale
    Progression,
    Shift,
    Delay,
    Strum,         // spreads a chord's notes over a short window (a strummed guitar)
    Humanize,      // final-stage performance feel: swing + timing/velocity jitter
    // I/O
    MidiIn,
    Output
};

// Which ports a module exposes (decorative until wiring lands, but the shapes
// already paint them). Derivable from the type: generators and MIDI In are
// sources, Output is the one sink, modulators have both.
inline bool moduleHasInputPort (ModuleType t)
{
    return t == ModuleType::Arp || t == ModuleType::Quantize
        || t == ModuleType::ScaleMod || t == ModuleType::Progression
        || t == ModuleType::Shift || t == ModuleType::Delay
        || t == ModuleType::Strum || t == ModuleType::Humanize
        || t == ModuleType::Output;
}

inline bool moduleHasOutputPort (ModuleType t)
{
    return t != ModuleType::Output;
}

struct ModuleDescriptor
{
    ModuleType  type;
    ModuleKind  kind;
    const char* name;

    juce::Colour familyColour() const
    {
        const auto& s = CurrentTheme::active();
        switch (kind)
        {
            case ModuleKind::Generator: return s.genFill;
            case ModuleKind::Modulator: return s.modFill;
            case ModuleKind::IO:        return s.ioFill;
        }
        return s.genFill;
    }
};

// The palette, in display order: generators, modulators, I/O. The Scale
// modulator shares its display name with the Scale generator on purpose (the
// user asked for a "scale modulator"); shape and colour keep them apart, and
// their persistence ids differ ("Scale" vs "ScaleMod").
inline const std::array<ModuleDescriptor, 15>& moduleCatalogue()
{
    static const std::array<ModuleDescriptor, 15> kCatalogue = {{
        { ModuleType::Random,      ModuleKind::Generator, "Random"      },
        { ModuleType::ScaleGen,    ModuleKind::Generator, "Scale"       },
        { ModuleType::Lfo,         ModuleKind::Generator, "LFO"         },
        { ModuleType::Chord,       ModuleKind::Generator, "Chord"       },
        { ModuleType::Drone,       ModuleKind::Generator, "Drone"       },
        { ModuleType::Arp,         ModuleKind::Modulator, "Arp"         },
        { ModuleType::Quantize,    ModuleKind::Modulator, "Quantize"    },
        { ModuleType::ScaleMod,    ModuleKind::Modulator, "Scale"       },
        { ModuleType::Progression, ModuleKind::Modulator, "Progression" },
        { ModuleType::Shift,       ModuleKind::Modulator, "Shift"       },
        { ModuleType::Delay,       ModuleKind::Modulator, "Delay"       },
        { ModuleType::Strum,       ModuleKind::Modulator, "Strum"       },
        { ModuleType::Humanize,    ModuleKind::Modulator, "Humanize"    },
        { ModuleType::MidiIn,      ModuleKind::IO,        "MIDI In"     },
        { ModuleType::Output,      ModuleKind::IO,        "Output"      }
    }};
    return kCatalogue;
}

inline const ModuleDescriptor& descriptorFor (ModuleType type)
{
    for (const auto& d : moduleCatalogue())
        if (d.type == type)
            return d;
    return moduleCatalogue().front();
}

// Stable string ids for state persistence, so a saved layout survives an enum
// reorder.
inline juce::String moduleTypeToString (ModuleType type)
{
    switch (type)
    {
        case ModuleType::Arp:      return "Arp";
        case ModuleType::Random:   return "Random";
        case ModuleType::ScaleGen: return "Scale";
        case ModuleType::Lfo:      return "LFO";
        case ModuleType::Chord:    return "Chord";
        case ModuleType::Drone:    return "Drone";
        case ModuleType::Quantize:    return "Quantize";
        case ModuleType::ScaleMod:    return "ScaleMod";
        case ModuleType::Progression: return "Progression";
        case ModuleType::Shift:       return "Shift";
        case ModuleType::Delay:       return "Delay";
        case ModuleType::Strum:       return "Strum";
        case ModuleType::Humanize:    return "Humanize";
        case ModuleType::MidiIn:      return "MidiIn";
        case ModuleType::Output:      return "Output";
    }
    return "Arp";
}

inline ModuleType moduleTypeFromString (const juce::String& s)
{
    if (s == "Random")   return ModuleType::Random;
    if (s == "Scale")    return ModuleType::ScaleGen;
    if (s == "LFO")      return ModuleType::Lfo;
    if (s == "Chord")    return ModuleType::Chord;
    if (s == "Drone")    return ModuleType::Drone;
    if (s == "Quantize") return ModuleType::Quantize;
    if (s == "ScaleMod") return ModuleType::ScaleMod;
    if (s == "Progression") return ModuleType::Progression;
    if (s == "Shift")    return ModuleType::Shift;
    if (s == "Delay")    return ModuleType::Delay;
    if (s == "Strum")    return ModuleType::Strum;
    if (s == "Humanize") return ModuleType::Humanize;
    if (s == "MidiIn")   return ModuleType::MidiIn;
    if (s == "Output")   return ModuleType::Output;
    return ModuleType::Arp;
}

// The I/O modules' one setting. Semantics differ per type: MIDI In filters
// which host channel enters the graph (0 = All), Output stamps outgoing
// events with its channel (1..16, no All). Other module types ignore it.
inline int defaultChannelFor (ModuleType type)
{
    return type == ModuleType::Output ? 1 : 0;
}
