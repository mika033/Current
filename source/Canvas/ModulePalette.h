#pragma once

#include <array>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ModuleComponent.h"

// The tray below the canvas (requirements doc "Module palette"): one chip
// per Phase 1 module type, dragged onto CanvasComponent to spawn an
// instance. Scrollable/collapsible tray behaviour for iPad is deferred -
// four chips fit a fixed row at Phase 1's native canvas width.
class ModulePalette : public juce::Component
{
public:
    ModulePalette();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Chip : public juce::Component
    {
    public:
        explicit Chip (ModuleType type);

        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        ModuleType moduleType;
    };

    std::array<std::unique_ptr<Chip>, 4> chips;
};
