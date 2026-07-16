#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ModuleTypes.h"

// A single placed module on the canvas: a draggable node the user can move
// around. Generators paint as squares, modulators as circles, I/O modules as
// triangles (MIDI In points right, Output left — per the requirements' shape
// encoding); the family colour comes from the theme. Ports are drawn as edge
// dots for visual completeness — wiring them up is a later phase, so they are
// decorative for now.
//
// The component owns only its appearance and gestures; the canvas owns the
// model. Position changes and double-clicks are reported back through the
// callbacks so the canvas can persist them and open the settings placeholder.
class ModuleComponent : public juce::Component
{
public:
    static constexpr int kSize = 84;   // square side / circle diameter

    ModuleComponent (int moduleId, ModuleType type);

    int        moduleId() const { return id; }
    ModuleType moduleType() const { return type; }

    void setSelected (bool shouldBeSelected);
    bool isSelected() const { return selected; }

    // Small secondary line under the name (the I/O modules show their channel
    // here). Empty = no sublabel.
    void setSublabel (const juce::String& text);

    // id, new top-left position within the canvas (called live during a drag).
    std::function<void (int, juce::Point<int>)> onMoved;
    // Fired on double-click — the canvas opens the settings placeholder.
    std::function<void (ModuleComponent&)> onOpenSettings;
    // Fired on a single click / drag start so the canvas can update selection.
    std::function<void (ModuleComponent&)> onSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    int          id;
    ModuleType   type;
    bool         selected = false;
    juce::String sublabel;
    juce::ComponentDragger dragger;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleComponent)
};
