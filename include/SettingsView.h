#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

class CurrentAudioProcessor;

// The settings space: shown in place of the canvas + palette when the menu
// bar's Settings button is toggled on (MainView owns the swap). Layout follows
// LAM's Settings tab — stacked rounded panels, each a section-name column
// followed by uniform label-over-control columns:
//
//  - Global panel: the Theme switch (moved here from the menu bar; same
//    click-to-cycle button driving the Theme parameter).
//  - Audition Synth panel: the enable toggle and the five tone dials
//    (Character, Cutoff, Env Amt, Decay, Space — signal-flow order: voice
//    tone, then insert FX). Hidden in the AU MIDI-FX, which has no audio
//    output for the synth to use.
//
// The dials are APVTS-attached, and each label doubles as the live value
// readout ("Cutoff: 920 Hz") — the ModuleWindow dial idiom. Touching a control
// additionally reports it to the bottom help bar via onFeedback.
class SettingsView : public juce::Component,
                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    SettingsView (CurrentAudioProcessor& processor,
                  std::function<void()> onThemeChanged);
    ~SettingsView() override;

    // Help-bar reporting (message, help key). Wired by MainView into its
    // showFeedback funnel.
    std::function<void (const juce::String&, const juce::String&)> onFeedback;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::AudioProcessorValueTreeState& state;
    std::function<void()> themeChanged;

    // Theme is a two-value choice, so a click-to-toggle button (label reads
    // the active theme name) replaces a dropdown — same control the menu bar
    // used to carry, relocated here.
    juce::Label      themeLabel;
    juce::TextButton themeButton;

    bool showSynth = false;
    juce::ToggleButton synthEnableToggle;

    // One dial column: label above (live "name: value" readout), rotary below.
    struct SynthDial
    {
        juce::Label  label;
        juce::Slider dial;
        juce::String baseName;
        juce::String helpKey;
        std::function<juce::String (double)> format;
        std::unique_ptr<SliderAttachment> attachment;
    };
    std::array<SynthDial, 5> dials;
    std::unique_ptr<ButtonAttachment> synthEnableAtt;

    void setupDial (SynthDial& d, const juce::String& paramID,
                    const juce::String& baseName,
                    const juce::String& helpKey,
                    std::function<juce::String (double)> format);
    void refreshDialLabel (SynthDial& d);

    // Set the theme button text from the current Theme parameter value.
    void refreshThemeButtonText();

    // Keeps the button label (and the applied skin) in sync with external
    // writes to the Theme parameter — state restore, preset apply, automation.
    void parameterChanged (const juce::String& paramID, float newValue) override;

    // Cached in resized(), consumed by paint() for the panel frames + titles.
    juce::Rectangle<int> globalPanel, synthPanel;

    // Layout metrics from LAM's Settings tab: uniform columns of
    // label (14) + gap (4) + control area (70) inside a 14 px panel inset.
    static constexpr int kPanelInset = 14;
    static constexpr int kPanelGap   = 10;
    static constexpr int kLabelH     = 14;
    static constexpr int kLabelGap   = 4;
    static constexpr int kDialSize   = 70;
    static constexpr int kPanelH     = kPanelInset * 2 + kLabelH + kLabelGap + kDialSize;
    static constexpr int kSlotW      = 100;
    static constexpr int kColGap     = 10;
    static constexpr int kComboH     = 26;
    static constexpr int kToggleW    = 28;
    static constexpr int kToggleH    = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsView)
};
