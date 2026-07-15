#pragma once

#include <vector>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ModuleComponent.h"

// The central canvas (requirements doc "Canvas"): a drop target for
// ModulePalette chips and the surface modules get dragged around on.
// No ports, no connections, no selection model yet - Phase 1 scope is
// drag-in + move only.
class CanvasComponent : public juce::Component,
                         public juce::DragAndDropTarget
{
public:
    CanvasComponent();

    // Set by the editor so double-clicking a module can open the
    // placeholder settings dialog without CanvasComponent knowing about
    // InlineDialog directly.
    std::function<void (const juce::String& moduleName)> onRequestModuleSettings;

    void paint (juce::Graphics&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    void addModule (ModuleType type, juce::Point<int> centrePosition);

    std::vector<std::unique_ptr<ModuleComponent>> modules;
};
