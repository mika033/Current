#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// The bar above the canvas. Phase 2 carries the global settings only — root
// and scale — plus a Theme switch. (Quantizing lives in the Quantize / Scale
// modules now; the old global Quantize toggle is gone.) The Edit and
// Load/Save menus described in the requirements are deferred (Phase 2 has no
// Load/Save), so they aren't shown yet.
//
// The combos are bound straight to the processor's APVTS, so the global
// settings are real parameters that persist and (later) automate.
class MenuBar : public juce::Component
{
public:
    MenuBar (juce::AudioProcessorValueTreeState& apvts,
             std::function<void()> onThemeChanged);
    ~MenuBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::AudioProcessorValueTreeState& state;

    juce::Label    titleLabel;

    juce::Label    rootLabel, scaleLabel;
    juce::ComboBox rootCombo, scaleCombo;

    juce::Label    themeLabel;
    juce::ComboBox themeCombo;

    std::unique_ptr<ComboAttachment> rootAtt, scaleAtt, themeAtt;

    std::function<void()> themeChanged;

    void populateFromChoiceParam (juce::ComboBox& combo, const juce::String& paramID);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MenuBar)
};
