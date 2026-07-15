#include "MenuBar.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "CurrentLookAndFeel.h"

MenuBar::MenuBar (juce::AudioProcessorValueTreeState& apvts,
                  std::function<void()> onThemeChanged)
    : state (apvts), themeChanged (std::move (onThemeChanged))
{
    titleLabel.setText ("Current", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    auto configLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };

    configLabel (rootLabel,  "Root");
    configLabel (scaleLabel, "Scale");

    addAndMakeVisible (rootCombo);
    addAndMakeVisible (scaleCombo);
    populateFromChoiceParam (rootCombo,  ParamIDs::root);
    populateFromChoiceParam (scaleCombo, ParamIDs::scale);
    rootAtt  = std::make_unique<ComboAttachment> (state, ParamIDs::root,  rootCombo);
    scaleAtt = std::make_unique<ComboAttachment> (state, ParamIDs::scale, scaleCombo);

    addAndMakeVisible (quantizeToggle);
    quantizeAtt = std::make_unique<ButtonAttachment> (state, ParamIDs::quantize, quantizeToggle);

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

    titleLabel.setColour (juce::Label::textColourId, s.text);
}

void MenuBar::resized()
{
    auto area = getLocalBounds().reduced (12, 0);

    titleLabel.setBounds (area.removeFromLeft (110).withSizeKeepingCentre (110, 24));

    constexpr int comboW = 92;
    constexpr int labelW = 46;
    constexpr int gap    = 8;
    const int rowH = 26;

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

    // Quantize toggle stands alone (its own caption is the toggle text).
    quantizeToggle.setBounds (area.removeFromLeft (110).withSizeKeepingCentre (110, rowH));
    area.removeFromLeft (gap);

    // Theme sits at the far right.
    auto rightSlot = area.removeFromRight (labelW + 90 + gap);
    rightSlot = rightSlot.withSizeKeepingCentre (rightSlot.getWidth(), rowH);
    themeLabel.setBounds (rightSlot.removeFromLeft (labelW));
    rightSlot.removeFromLeft (4);
    themeCombo.setBounds (rightSlot.removeFromLeft (90));
}
