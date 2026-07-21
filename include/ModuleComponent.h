#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ModuleTypes.h"

// A single placed module on the canvas: a draggable node the user can move
// around. Generators paint as squares, modulators as circles, I/O modules as
// triangles (MIDI In points right, Output left — per the requirements' shape
// encoding); the family colour comes from the theme. Ports are live: pressing
// the output port starts the connect gesture (a cable drag the canvas owns)
// instead of a node move.
//
// The component owns only its appearance and gestures; the canvas owns the
// model. Position changes, double-clicks, and port drags are reported back
// through the callbacks so the canvas can persist them, open the settings
// dialog, and run the cable gesture.
class ModuleComponent : public juce::Component
{
public:
    static constexpr int kSize = 84;   // square side / circle diameter

    // Hit radius around a port centre — deliberately larger than the painted
    // dot (the requirements ask for enlarged port hit targets).
    static constexpr int kPortHitRadius = 14;

    ModuleComponent (int moduleId, ModuleType type);

    int        moduleId() const { return id; }
    ModuleType moduleType() const { return type; }

    void setSelected (bool shouldBeSelected);
    bool isSelected() const { return selected; }

    // Small secondary line under the name (the I/O modules show their channel
    // here). Empty = no sublabel.
    void setSublabel (const juce::String& text);

    // Port centres in the parent's (canvas's) coordinate space — where cables
    // attach. Only meaningful for ports the type actually has.
    juce::Point<float> inputPortCentre() const;
    juce::Point<float> outputPortCentre() const;

    // id, new top-left position within the canvas (called live during a drag).
    std::function<void (int, juce::Point<int>)> onMoved;
    // The node-move drag stream (not the port/cable gesture) — the canvas
    // forwards these to the tray's remove-zone logic, so dragging a node onto
    // the palette tray deletes it.
    std::function<void (const juce::MouseEvent&)> onNodeDrag;
    std::function<void (ModuleComponent&, const juce::MouseEvent&)> onNodeDragEnd;
    // Fired by a tap on the ✕ badge (visible while selected). The receiver
    // must defer the actual deletion — this component is mid-mouse-callback.
    std::function<void (ModuleComponent&)> onDelete;
    // Fired on double-click — the canvas opens the settings dialog.
    std::function<void (ModuleComponent&)> onOpenSettings;
    // Fired on a single click / drag start so the canvas can update selection.
    std::function<void (ModuleComponent&)> onSelected;
    // The connect gesture: begins on an output-port press; the canvas draws
    // the live cable and resolves the drop target.
    std::function<void (ModuleComponent&, const juce::MouseEvent&)> onPortDragStart;
    std::function<void (const juce::MouseEvent&)> onPortDrag;
    std::function<void (const juce::MouseEvent&)> onPortDragEnd;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    // The ✕ badge in the top-right corner while selected — the touch path to
    // deletion (Delete key stays the desktop shortcut).
    juce::Rectangle<float> deleteBadgeBounds() const;

    int          id;
    ModuleType   type;
    bool         selected = false;
    bool         draggingCable = false;
    bool         pressedDeleteBadge = false;
    juce::String sublabel;
    juce::ComponentDragger dragger;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleComponent)
};
