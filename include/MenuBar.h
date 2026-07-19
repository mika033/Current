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
class MenuBar : public juce::Component
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

    juce::Label    themeLabel;
    juce::ComboBox themeCombo;

    // Standalone-only internal transport (never created in plugin wrappers):
    // the Play toggle is the leftmost item, immediately followed by the BPM
    // stepper ([ + ] value [ - ]).
    bool             showTransport = false;
    juce::TextButton playButton { "Play" };
    StepperControl   bpmStepper;

    std::unique_ptr<ComboAttachment> rootAtt, scaleAtt, themeAtt;

    std::function<void()> themeChanged;

    void populateFromChoiceParam (juce::ComboBox& combo, const juce::String& paramID);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MenuBar)
};
