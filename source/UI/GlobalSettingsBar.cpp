#include "GlobalSettingsBar.h"
#include "ColourScheme.h"

namespace
{
    const juce::StringArray rootNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const juce::StringArray scaleNames { "Major", "Minor", "Dorian", "Phrygian", "Lydian",
                                          "Mixolydian", "Locrian", "Harmonic Minor", "Melodic Minor", "Chromatic" };
}

GlobalSettingsBar::GlobalSettingsBar()
{
    rootBox.addItemList (rootNames, 1);
    rootBox.setSelectedItemIndex (0);
    addAndMakeVisible (rootBox);

    scaleBox.addItemList (scaleNames, 1);
    scaleBox.setSelectedItemIndex (0);
    addAndMakeVisible (scaleBox);

    // No functional effect yet (Phase 1 modules run with fixed defaults,
    // nothing reads these settings) - just the toggle affordance existing.
    quantizeButton.setClickingTogglesState (true);
    quantizeButton.setToggleState (true, juce::dontSendNotification);
    quantizeButton.onClick = [this]
    {
        quantizeButton.setButtonText (quantizeButton.getToggleState() ? "Quantize: On" : "Quantize: Off");
    };
    addAndMakeVisible (quantizeButton);

    rootLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (rootLabel);

    scaleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (scaleLabel);
}

void GlobalSettingsBar::paint (juce::Graphics& g)
{
    g.setColour (active().sectionBoxBg);
    g.fillAll();

    g.setColour (active().panelBorder);
    g.drawLine (0.0f, (float) getHeight() - 0.5f, (float) getWidth(), (float) getHeight() - 0.5f, 1.0f);
}

void GlobalSettingsBar::resized()
{
    constexpr int controlH = 26;
    constexpr int pad = 12;
    auto bounds = getLocalBounds().reduced (pad, (getHeight() - controlH) / 2);

    rootLabel.setBounds (bounds.removeFromLeft (36));
    bounds.removeFromLeft (6);
    rootBox.setBounds (bounds.removeFromLeft (70));
    bounds.removeFromLeft (pad);

    scaleLabel.setBounds (bounds.removeFromLeft (42));
    bounds.removeFromLeft (6);
    scaleBox.setBounds (bounds.removeFromLeft (130));
    bounds.removeFromLeft (pad);

    quantizeButton.setBounds (bounds.removeFromLeft (110));
}
