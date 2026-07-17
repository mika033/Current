#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <memory>
#include "ModuleTypes.h"
#include "ModuleComponent.h"
#include "PluginProcessor.h"   // ModuleInstance + processor model

class CurrentAudioProcessorEditor;
class InlineDialog;

// The central drop surface. Modules dragged from the palette land here and can
// be moved around; double-clicking one opens its (empty) settings placeholder.
// The canvas is the bridge between the on-screen nodes and the processor's
// module model — it rebuilds its nodes from the model on construction and writes
// adds/moves back to it.
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

    // The per-module settings dialogs (I/O channel; Arp / Random / Scale
    // settings) and the node sublabels that mirror the choices.
    void openChannelDialog (ModuleComponent& node);
    void openArpDialog (ModuleComponent& node);
    void openRandomDialog (ModuleComponent& node);
    void openScaleGenDialog (ModuleComponent& node);
    void openLfoDialog (ModuleComponent& node);
    void openShiftDialog (ModuleComponent& node);
    static juce::String channelSublabel (ModuleType type, int channel);
    static juce::String rateSublabel (const ModuleSettings& settings);
    static juce::String shiftSublabel (const ModuleSettings& settings);

    // Shared dialog controls, one add/read pair per shared setting (see
    // modules.md "Shared settings"). Every dialog builds its combos through
    // these so a setting looks and reads identically in every module.
    // `firstRate` narrows the rate list's start (the Scale generator has no
    // 1/32); `modeCount` narrows the mode list (the Scale generator offers
    // Up/Down only).
    void addRootScaleControls (InlineDialog&, const ModuleSettings&);
    static void readRootScaleControls (const InlineDialog&, ModuleSettings&);
    static void addRateControl (InlineDialog&, const ModuleSettings&, int firstRate = 0);
    static void readRateControl (const InlineDialog&, ModuleSettings&, int firstRate = 0);
    static void addRepeatControl (InlineDialog&, const ModuleSettings&);
    static void readRepeatControl (const InlineDialog&, ModuleSettings&);
    static void addModeControl (InlineDialog&, const ModuleSettings&, int modeCount);
    static void readModeControl (const InlineDialog&, ModuleSettings&);
    static void addOctavesControl (InlineDialog&, const ModuleSettings&);
    static void readOctavesControl (const InlineDialog&, ModuleSettings&);

    // "Global" followed by the choices of a global APVTS parameter — the
    // dialogs' root/scale lists, sourced from the parameter so they can't
    // drift from the menu bar.
    juce::StringArray choicesWithGlobal (const char* paramID) const;

    CurrentAudioProcessor&        proc;
    CurrentAudioProcessorEditor&  owner;

    std::vector<std::unique_ptr<ModuleComponent>> nodes;
    ModuleComponent* selectedNode = nullptr;

    bool dragHovering = false;   // paint a drop-hint border while a drag is over us

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};
