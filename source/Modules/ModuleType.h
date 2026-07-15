#pragma once

#include <array>
#include <juce_core/juce_core.h>

// Phase 1 palette per the requirements doc ("Palette offers two generators
// and two modulators only, specific modules to be chosen"). Random/Arp and
// Quantize/Rhythm were picked as simple, visually distinct representatives -
// swapping the roster later is a one-line change here, nothing downstream
// depends on which four these are.
enum class ModuleKind
{
    generator,
    modulator
};

enum class ModuleType
{
    random,
    arp,
    quantize,
    rhythm
};

struct ModuleTypeInfo
{
    ModuleType type;
    ModuleKind kind;
    const char* name;
};

// Generators draw as squares, modulators as circles - already decided in
// the requirements doc's Canvas section, independent of the open "family
// colour" question.
inline const ModuleTypeInfo& infoFor (ModuleType type)
{
    static constexpr std::array<ModuleTypeInfo, 4> table {{
        { ModuleType::random,   ModuleKind::generator, "Random" },
        { ModuleType::arp,      ModuleKind::generator, "Arp" },
        { ModuleType::quantize, ModuleKind::modulator, "Quantize" },
        { ModuleType::rhythm,   ModuleKind::modulator, "Rhythm" },
    }};

    for (auto& entry : table)
        if (entry.type == type)
            return entry;

    jassertfalse;
    return table[0];
}

inline const std::array<ModuleType, 4>& paletteModuleTypes()
{
    static constexpr std::array<ModuleType, 4> types {
        ModuleType::random, ModuleType::arp, ModuleType::quantize, ModuleType::rhythm
    };
    return types;
}
