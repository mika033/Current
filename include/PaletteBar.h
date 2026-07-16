#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "ModuleTypes.h"

// The palette tray below the canvas: the set of modules that can be dragged onto
// it — one chip per catalogue entry (generators, modulators, I/O). Each item is
// a drag source; the drop is handled by the Canvas.
//
// A DragAndDropContainer must be an ancestor of both the palette items and the
// canvas — that's the editor (MainView's parent), which derives from it.
class PaletteBar : public juce::Component
{
public:
    PaletteBar();
    ~PaletteBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // A single draggable chip in the tray. Mirrors the canvas node's
    // shape/colour so the user sees what they're about to place.
    class PaletteItem : public juce::Component
    {
    public:
        explicit PaletteItem (ModuleType type);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        ModuleType type;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaletteItem)
    };

    std::vector<std::unique_ptr<PaletteItem>> items;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaletteBar)
};
