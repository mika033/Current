#include "MenuBar.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"

MenuBar::MenuBar (CurrentAudioProcessor& processor,
                  std::function<void()> onThemeChanged)
    : state (processor.apvts()), themeChanged (std::move (onThemeChanged))
{
    auto configLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };

    configLabel (rootLabel,  "Root");
    configLabel (scaleLabel, "Scale");

    // Standalone-only internal transport (the LAM approach): no host
    // transport exists, so Play and Tempo live here. The button text swaps
    // Play<->Stop with the toggle state so it reads correctly even where the
    // pressed state isn't visually obvious.
    showTransport = processor.isStandalone();
    if (showTransport)
    {
        playButton.setClickingTogglesState (true);
        playButton.onClick = [this, &processor]()
        {
            const bool on = playButton.getToggleState();
            processor.setStandalonePlay (on);
            playButton.setButtonText (on ? "Stop" : "Play");
        };
        addAndMakeVisible (playButton);

        // 20 BPM floor matches LAM's Tempo stepper (safe for anything sized
        // from a beat length); 300 is the musically useful ceiling.
        bpmStepper.setRange (20.0, 300.0, 1.0);
        bpmStepper.setValue (processor.getInternalBpm(), juce::dontSendNotification);
        bpmStepper.onValueChange = [&processor] (double v)
        {
            processor.setInternalBpm (v);
        };
        addAndMakeVisible (bpmStepper);
    }

    addAndMakeVisible (rootCombo);
    addAndMakeVisible (scaleCombo);
    populateFromChoiceParam (rootCombo,  ParamIDs::root);
    populateFromChoiceParam (scaleCombo, ParamIDs::scale);
    rootAtt  = std::make_unique<ComboAttachment> (state, ParamIDs::root,  rootCombo);
    scaleAtt = std::make_unique<ComboAttachment> (state, ParamIDs::scale, scaleCombo);

    configLabel (themeLabel, "Theme");
    addAndMakeVisible (themeCombo);
    populateFromChoiceParam (themeCombo, ParamIDs::theme);
    themeAtt = std::make_unique<ComboAttachment> (state, ParamIDs::theme, themeCombo);
    // The attachment writes the parameter; we additionally repaint the editor so
    // the theme swap is visible immediately (the parameter has no audio effect).
    themeCombo.onChange = [this]() { if (themeChanged) themeChanged(); };
}

MenuBar::~MenuBar() = default;

void MenuBar::populateFromChoiceParam (juce::ComboBox& combo, const juce::String& paramID)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID)))
        combo.addItemList (choice->choices, 1);
}

void MenuBar::paint (juce::Graphics& g)
{
    const auto& s = CurrentTheme::active();
    auto b = getLocalBounds().toFloat();
    g.setColour (s.panelBg);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (s.panelBorder);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
}

void MenuBar::resized()
{
    auto area = getLocalBounds().reduced (12, 0);

    constexpr int comboW = 92;
    constexpr int labelW = 46;
    constexpr int gap    = 8;
    const int rowH = 26;

    // In the Standalone the transport leads the bar: Play first, then the BPM
    // stepper, ahead of the global Root / Scale combos.
    if (showTransport)
    {
        auto playSlot = area.removeFromLeft (56).withSizeKeepingCentre (56, rowH);
        playButton.setBounds (playSlot);
        area.removeFromLeft (gap);

        auto bpmSlot = area.removeFromLeft (104).withSizeKeepingCentre (104, rowH);
        bpmStepper.setBounds (bpmSlot);
        area.removeFromLeft (gap);
    }

    auto place = [&] (juce::Label& lbl, juce::Component& ctl, int w)
    {
        auto slot = area.removeFromLeft (labelW + w + gap);
        slot = slot.withSizeKeepingCentre (slot.getWidth(), rowH);
        lbl.setBounds (slot.removeFromLeft (labelW));
        slot.removeFromLeft (4);
        ctl.setBounds (slot.removeFromLeft (w));
        area.removeFromLeft (gap);
    };

    place (rootLabel,  rootCombo,  comboW);
    place (scaleLabel, scaleCombo, comboW);

    // Theme sits at the far right.
    auto rightSlot = area.removeFromRight (labelW + 90 + gap);
    rightSlot = rightSlot.withSizeKeepingCentre (rightSlot.getWidth(), rowH);
    themeLabel.setBounds (rightSlot.removeFromLeft (labelW));
    rightSlot.removeFromLeft (4);
    themeCombo.setBounds (rightSlot.removeFromLeft (90));
}
