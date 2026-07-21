#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "ModuleTypes.h"

// The palette tray below the canvas: the set of modules that can be dragged onto
// it — one chip per catalogue entry (I/O, generators, modulators). Each item is
// a drag source; the drop is handled by the Canvas.
//
// Chips keep their full size at every window width: they live in a horizontal
// strip inside a Viewport whose scrollbar sits along the tray's bottom edge.
// A filter row above the strip (In/Out / Generators / Modulators checkboxes)
// hides whole families; filter state is per-editor-instance, not persisted.
//
// The tray doubles as the remove zone for canvas nodes: while a node is being
// dragged the tray arms (thin danger border), and when the pointer is over it
// the tray paints a full "release to remove" overlay. Canvas owns the gesture;
// MainView wires the two together.
//
// A DragAndDropContainer must be an ancestor of both the palette items and the
// canvas — that's the editor (MainView's parent), which derives from it.
class PaletteBar : public juce::Component
{
public:
    PaletteBar();
    ~PaletteBar() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // remove-zone highlight
    void resized() override;

    // Remove-zone feedback for the drag-a-node-to-the-tray delete gesture:
    // `armed` = a node drag is in flight anywhere, `hot` = the pointer is over
    // the tray right now (releasing deletes the node).
    void setRemoveDragState (bool armed, bool hot);

private:
    // A single draggable chip in the tray. Mirrors the canvas node's
    // shape/colour so the user sees what they're about to place.
    class PaletteItem : public juce::Component
    {
    public:
        explicit PaletteItem (ModuleType type);
        ModuleKind kind() const { return descriptorFor (type).kind; }
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        ModuleType type;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaletteItem)
    };

    // Applies the family filters: chip visibility plus the strip's total width
    // (the viewport reads the scroll range off the strip size).
    void rebuildStrip();
    bool kindShown (ModuleKind kind) const;

    juce::ToggleButton ioFilter, genFilter, modFilter;
    juce::Viewport  viewport;
    juce::Component strip;   // viewport content: the chips, laid out left to right
    std::vector<std::unique_ptr<PaletteItem>> items;

    bool removeArmed = false;
    bool removeHot   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaletteBar)
};
