#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Modules/ModuleType.h"

// Shared paint routine for a module's shape (square for generators, circle
// for modulators - requirements doc "Canvas") + name label. Used both by
// the live ModuleComponent on the canvas and by ModulePalette's drag chips
// so the palette preview and the dropped module look like the same object.
void paintModuleShape (juce::Graphics&, juce::Rectangle<float> bounds, ModuleType type);

static constexpr int kModuleBoxSize = 84;

// A module instance sitting on the canvas. Phase 1: can be moved by
// dragging its body, and double-click opens the (currently empty)
// placeholder settings dialog. No ports, no connections, no delete yet -
// those are later-phase interactions per the requirements doc.
class ModuleComponent : public juce::Component
{
public:
    explicit ModuleComponent (ModuleType type);

    ModuleType getType() const { return moduleType; }

    std::function<void (ModuleComponent&)> onOpenSettings;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    ModuleType moduleType;
    juce::ComponentDragger dragger;
    juce::ComponentBoundsConstrainer constrainer;
};
