#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <memory>
#include "ModuleTypes.h"
#include "ModuleComponent.h"
#include "PluginProcessor.h"   // ModuleInstance + processor model

class CurrentAudioProcessorEditor;
class InlineDialog;
class ModuleWindow;

// The central drop surface. Modules dragged from the palette land here, can be
// moved around, and are wired port-to-port: dragging from a node's output port
// draws a live cable that connects on release over a module with an input
// port. Cables paint as curves with a flow arrow, can be selected by clicking
// near them, and Delete removes the selection (cable or node).
// The canvas is the bridge between the on-screen nodes and the processor's
// model (modules + connections) — it rebuilds its nodes from the model on
// construction and writes adds/moves/connects back to it; cables are painted
// straight from the model, so they need no components of their own.
class Canvas : public juce::Component,
               public juce::DragAndDropTarget,
               private juce::ChangeListener
{
public:
    Canvas (CurrentAudioProcessor& processor, CurrentAudioProcessorEditor& editor);
    ~Canvas() override;

    // The palette encodes the dragged module type in the drag description via
    // this key, so isInterestedInDragSource can recognise a palette drag.
    static juce::var makeDragDescription (ModuleType type);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // click empty space = deselect
    bool keyPressed (const juce::KeyPress&) override;     // Delete removes selection

    // DragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit  (const SourceDetails&) override;
    void itemDropped   (const SourceDetails&) override;

private:
    // Model replaced behind our back (host state restore) — rebuild the nodes.
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void addNodeComponent (const ModuleInstance& instance);
    void rebuildFromModel();
    void selectNode (ModuleComponent* node);
    void deleteSelected();

    // --- Wiring: the connect gesture and cable rendering --------------------
    // The gesture starts in ModuleComponent (output-port press) and is owned
    // here: the live cable follows the mouse, and releasing over a module with
    // an input port asks the processor for the connection (its canConnect is
    // the single validity rule — an invalid drop simply snaps back).
    void beginCableDrag (ModuleComponent& fromNode, const juce::MouseEvent&);
    void updateCableDrag (const juce::MouseEvent&);
    void endCableDrag (const juce::MouseEvent&);
    void paintCables (juce::Graphics&);
    juce::Path cablePath (juce::Point<float> from, juce::Point<float> to) const;
    ModuleComponent* nodeForId (int id) const;

    // The per-module settings dialogs (I/O channel; Arp / Random / Scale
    // settings) and the node sublabels that mirror the choices.
    void openChannelDialog (ModuleComponent& node);
    void openArpDialog (ModuleComponent& node);
    void openRandomDialog (ModuleComponent& node);
    void openScaleGenDialog (ModuleComponent& node);
    void openLfoDialog (ModuleComponent& node);
    void openQuantizeDialog (ModuleComponent& node);
    void openScaleModDialog (ModuleComponent& node);
    void openProgressionDialog (ModuleComponent& node);
    void openShiftDialog (ModuleComponent& node);
    void openMirrorDialog (ModuleComponent& node);
    void openHarmonizerDialog (ModuleComponent& node);
    void openDelayDialog (ModuleComponent& node);
    void openStrumDialog (ModuleComponent& node);
    void openHumanizeDialog (ModuleComponent& node);
    void openChordDialog (ModuleComponent& node);
    void openDroneDialog (ModuleComponent& node);
    static juce::String channelSublabel (ModuleType type, int channel);
    static juce::String rateSublabel (const ModuleSettings& settings);
    static juce::String shiftSublabel (const ModuleSettings& settings);
    static juce::String mirrorSublabel (const ModuleSettings& settings);
    juce::String scaleModSublabel (const ModuleSettings& settings) const;
    static juce::String progressionSublabel (const ModuleSettings& settings);
    static juce::String chordSublabel (const ModuleSettings& settings);
    static juce::String droneSublabel (const ModuleSettings& settings);
    static juce::String strumSublabel (const ModuleSettings& settings);
    static juce::String harmonizerSublabel (const ModuleSettings& settings);

    // Shared dialog controls, one add/read pair per shared setting (see
    // modules.md "Shared settings"). Every dialog builds its combos through
    // these so a setting looks and reads identically in every module.
    // `scaleOffersOff` adds the "Off" (chromatic) scale choice for the modules
    // that support it (the pitch transformers plus Random/LFO); the
    // scale-walking generators pass false. `modeCount` narrows the mode list
    // (the Scale generator offers Up/Down only).
    // Every module now settles its settings through ModuleWindow. These menu
    // helpers target the window's three fixed menu slots (Root=0, Scale=1,
    // Rate/Length=2); the grid helpers take the 0..5 grid cell. Read helpers
    // look the control up by name, so they need no slot. Gate and octaves are
    // dials here (the design's knob-friendly values); the rest are combos.
    // Progression additionally swaps the grid for a custom body (its step list).
    void addRootScaleMenu (ModuleWindow&, const ModuleSettings&, bool scaleOffersOff);
    void readRootScaleMenu (const ModuleWindow&, ModuleSettings&, bool scaleOffersOff) const;
    static void addRateMenu (ModuleWindow&, const ModuleSettings&);
    static void readRateMenu (const ModuleWindow&, ModuleSettings&);
    static void addHoldLengthMenu (ModuleWindow&, const ModuleSettings&);
    static void readHoldLengthMenu (const ModuleWindow&, ModuleSettings&);
    static void addGateDial (ModuleWindow&, int slot, const ModuleSettings&);
    static void readGateDial (const ModuleWindow&, ModuleSettings&);
    static void addOctavesDial (ModuleWindow&, int slot, const ModuleSettings&);
    static void readOctavesDial (const ModuleWindow&, ModuleSettings&);
    static void addModeCombo (ModuleWindow&, int slot, const ModuleSettings&, int modeCount);
    static void readModeCombo (const ModuleWindow&, ModuleSettings&);
    static void addRepeatCombo (ModuleWindow&, int slot, const ModuleSettings&);
    static void readRepeatCombo (const ModuleWindow&, ModuleSettings&);
    static void addHoldRepeatCombo (ModuleWindow&, int slot, const ModuleSettings&);
    static void readHoldRepeatCombo (const ModuleWindow&, ModuleSettings&);
    // Shift and Delay share a signed transpose-amount dial whose unit label
    // reads off the Scale combo (semitones with Scale = Off, steps otherwise).
    // Both the dial and the Scale-combo callback are wired here so the two
    // modules' one genuinely identical control can't drift. `name` is the dial's
    // lookup key (they differ so nothing else has to); `range` is the ± span.
    static void addAmountDial (ModuleWindow&, int slot, const juce::String& name,
                               int value, int range);
    static int  readAmountDial (const ModuleWindow&, const juce::String& name);

    // "Global" followed by the choices of a global APVTS parameter — the
    // dialogs' root/scale lists, sourced from the parameter so they can't
    // drift from the menu bar.
    juce::StringArray choicesWithGlobal (const char* paramID) const;

    // The scale-override list and its index<->override mapping, shared by every
    // pitch module (InlineDialog and Random's ModuleWindow alike) so "Global /
    // Off / named" means the same thing everywhere. `offersOff` inserts "Off"
    // (the kScaleOff sentinel) as the second entry. Root has no Off, so its
    // combo is just choicesWithGlobal (index = override + 1).
    juce::StringArray scaleChoices (bool offersOff) const;
    static int scaleIndexForOverride (int scaleOverride, bool offersOff);
    static int scaleOverrideForIndex (int comboIndex, bool offersOff);

    CurrentAudioProcessor&        proc;
    CurrentAudioProcessorEditor&  owner;

    std::vector<std::unique_ptr<ModuleComponent>> nodes;
    ModuleComponent* selectedNode = nullptr;

    // The in-flight connect gesture (live cable from a node's output port to
    // the mouse) and the currently selected cable (Delete removes it).
    struct CableDrag { bool active = false; int fromId = 0; juce::Point<float> toPos; };
    CableDrag cableDrag;
    struct SelectedCable { bool valid = false; int fromId = 0; int toId = 0; };
    SelectedCable selectedCable;

    bool dragHovering = false;   // paint a drop-hint border while a drag is over us

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};
