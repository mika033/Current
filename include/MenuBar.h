#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "StepperControl.h"

class CurrentAudioProcessor;

// The bar above the canvas. Phase 2 carries the global settings only — root
// and scale — plus a Theme switch. (Quantizing lives in the Quantize / Scale
// modules now; the old global Quantize toggle is gone.) The Edit and
// Load/Save menus described in the requirements are deferred (Phase 2 has no
// Load/Save), so they aren't shown yet.
//
// In the Standalone the bar also carries the internal transport — a Play
// toggle and a Tempo stepper — because there is no host transport to sync
// to (the LAM approach; see the processor's internal-transport accessors).
// Plugin wrappers never show these.
//
// The combos are bound straight to the processor's APVTS, so the global
// settings are real parameters that persist and (later) automate.
class MenuBar : public juce::Component,
                private juce::AudioProcessorValueTreeState::Listener
{
public:
    MenuBar (CurrentAudioProcessor& processor,
             std::function<void()> onThemeChanged);
    ~MenuBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::AudioProcessorValueTreeState& state;

    juce::Label    rootLabel, scaleLabel;
    juce::ComboBox rootCombo, scaleCombo;

    // Theme is a two-value choice, so a click-to-toggle button (label reads the
    // active theme name) replaces a dropdown — cheaper real estate for two
    // options, and the text doubles as the value readout (the LAM approach).
    juce::Label      themeLabel;
    juce::TextButton themeButton;

    // Standalone-only internal transport (never created in plugin wrappers):
    // the Play toggle is the leftmost item, immediately followed by the BPM
    // stepper ([ + ] value [ - ]).
    bool             showTransport = false;
    juce::TextButton playButton { "Play" };
    StepperControl   bpmStepper;

    std::unique_ptr<ComboAttachment> rootAtt, scaleAtt;

    std::function<void()> themeChanged;

    void populateFromChoiceParam (juce::ComboBox& combo, const juce::String& paramID);

    // Set the theme button text from the current Theme parameter value.
    void refreshThemeButtonText();

    // Keeps the button label (and the applied skin) in sync with external
    // writes to the Theme parameter — state restore, preset apply, automation.
    void parameterChanged (const juce::String& paramID, float newValue) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MenuBar)
};
