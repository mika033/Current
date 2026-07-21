#include "MenuBar.h"
#include "PluginProcessor.h"
#include "HelpText.h"
#include "Theme.h"

MenuBar::MenuBar (CurrentAudioProcessor& processor,
                  std::function<void()> onSettingsClicked)
    : state (processor.apvts())
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
            if (onFeedback)
                onFeedback (on ? "Playing" : "Stopped", "transport.play");
        };
        addAndMakeVisible (playButton);

        // 20 BPM floor matches LAM's Tempo stepper (safe for anything sized
        // from a beat length); 300 is the musically useful ceiling.
        bpmStepper.setRange (20.0, 300.0, 1.0);
        bpmStepper.setValue (processor.getInternalBpm(), juce::dontSendNotification);
        bpmStepper.onValueChange = [this, &processor] (double v)
        {
            processor.setInternalBpm (v);
            if (onFeedback)
                onFeedback ("Tempo: " + juce::String ((int) v) + " BPM", "transport.tempo");
        };
        addAndMakeVisible (bpmStepper);
    }

    addAndMakeVisible (rootCombo);
    addAndMakeVisible (scaleCombo);
    populateFromChoiceParam (rootCombo,  ParamIDs::root);
    populateFromChoiceParam (scaleCombo, ParamIDs::scale);

    // Feedback for the global pair. Assigned before the attachments so their
    // initial sync runs through the feedbackArmed=false gate; the scale routes
    // each named scale to its own help.json line ("scale.minor" …).
    rootCombo.onChange = [this]()
    {
        if (feedbackArmed && onFeedback)
            onFeedback ("Root: " + rootCombo.getText(), "root");
    };
    scaleCombo.onChange = [this]()
    {
        if (feedbackArmed && onFeedback)
            onFeedback ("Scale: " + scaleCombo.getText(),
                        HelpText::keyForOption ("scale", scaleCombo.getText()));
    };

    rootAtt  = std::make_unique<ComboAttachment> (state, ParamIDs::root,  rootCombo);
    scaleAtt = std::make_unique<ComboAttachment> (state, ParamIDs::scale, scaleCombo);

    settingsButton.onClick = std::move (onSettingsClicked);
    addAndMakeVisible (settingsButton);

    feedbackArmed = true;
}

MenuBar::~MenuBar() = default;

void MenuBar::setSettingsOpen (bool open)
{
    settingsButton.setButtonText (open ? "Back" : "Settings");
}

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

    // Settings sits at the far right, wide enough for its longer "Settings"
    // reading so the width doesn't jump when the text flips to "Back".
    constexpr int settingsBtnW = 76;
    auto rightSlot = area.removeFromRight (settingsBtnW);
    rightSlot = rightSlot.withSizeKeepingCentre (settingsBtnW, rowH);
    settingsButton.setBounds (rightSlot);
}
