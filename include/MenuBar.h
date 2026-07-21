#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "StepperControl.h"

class CurrentAudioProcessor;

// The bar above the canvas. Phase 2 carries the global settings only — root
// and scale — plus the Settings button that swaps the canvas for the settings
// space (theme moved there). (Quantizing lives in the Quantize / Scale modules
// now; the old global Quantize toggle is gone.) The Edit and Load/Save menus
// described in the requirements are deferred (Phase 2 has no Load/Save), so
// they aren't shown yet.
//
// In the Standalone the bar also carries the internal transport — a Play
// toggle and a Tempo stepper — because there is no host transport to sync
// to (the LAM approach; see the processor's internal-transport accessors).
// Plugin wrappers never show these.
//
// The combos are bound straight to the processor's APVTS, so the global
// settings are real parameters that persist and (later) automate.
class MenuBar : public juce::Component
{
public:
    MenuBar (CurrentAudioProcessor& processor,
             std::function<void()> onSettingsClicked);
    ~MenuBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // MainView owns the open/closed state; it calls back here so the button
    // reads "Back" while the settings space is showing.
    void setSettingsOpen (bool open);

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::AudioProcessorValueTreeState& state;

    juce::Label    rootLabel, scaleLabel;
    juce::ComboBox rootCombo, scaleCombo;

    // Far right: toggles the settings space in and out (replacing the old
    // in-bar Theme switch, which now lives inside the settings space).
    juce::TextButton settingsButton { "Settings" };

    // Standalone-only internal transport (never created in plugin wrappers):
    // the Play toggle is the leftmost item, immediately followed by the BPM
    // stepper ([ + ] value [ - ]).
    bool             showTransport = false;
    juce::TextButton playButton { "Play" };
    StepperControl   bpmStepper;

    std::unique_ptr<ComboAttachment> rootAtt, scaleAtt;

    void populateFromChoiceParam (juce::ComboBox& combo, const juce::String& paramID);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MenuBar)
};
