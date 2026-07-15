#pragma once

#include <juce_graphics/juce_graphics.h>
#include "Theme.h"

// The catalogue of module types the canvas knows about. Phase 1 ships a
// deliberately tiny set — two generators and two modulators — enough to prove
// the canvas / palette / settings-placeholder loop. New modules (and the I/O
// kind) slot in by extending this enum and kCatalogue; nothing else in the
// canvas needs to change.

enum class ModuleKind
{
    Generator,   // drawn as a square (has outputs only, once ports land)
    Modulator,   // drawn as a circle
    IO           // MIDI In / Output — shape TBD; not in the Phase 1 palette
};

enum class ModuleType
{
    // Generators
    Arp,
    Random,
    // Modulators
    Quantize,
    Shift
};

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

// The Phase 1 palette, in display order. Two generators, two modulators.
inline const std::array<ModuleDescriptor, 4>& moduleCatalogue()
{
    static const std::array<ModuleDescriptor, 4> kCatalogue = {{
        { ModuleType::Arp,      ModuleKind::Generator, "Arp"      },
        { ModuleType::Random,   ModuleKind::Generator, "Random"   },
        { ModuleType::Quantize, ModuleKind::Modulator, "Quantize" },
        { ModuleType::Shift,    ModuleKind::Modulator, "Shift"    }
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
// reorder. Only the four Phase 1 types are covered.
inline juce::String moduleTypeToString (ModuleType type)
{
    switch (type)
    {
        case ModuleType::Arp:      return "Arp";
        case ModuleType::Random:   return "Random";
        case ModuleType::Quantize: return "Quantize";
        case ModuleType::Shift:    return "Shift";
    }
    return "Arp";
}

inline ModuleType moduleTypeFromString (const juce::String& s)
{
    if (s == "Random")   return ModuleType::Random;
    if (s == "Quantize") return ModuleType::Quantize;
    if (s == "Shift")    return ModuleType::Shift;
    return ModuleType::Arp;
}
