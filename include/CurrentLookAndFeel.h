#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// One LookAndFeel for the whole editor. Subclass of V4 so combos / buttons /
// labels / popup menus pick up the active theme's colours without a
// per-component setColour on every widget. applyScheme() pushes the current
// CurrentTheme scheme into the colour map; the editor calls it from its ctor and
// whenever the Theme combo changes.
class CurrentLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Shared control font size — combos, buttons, dialog body text all use this
    // so the plugin reads at one consistent size.
    static constexpr float kUiFontSize = 15.0f;

    CurrentLookAndFeel()
    {
        applyScheme (CurrentTheme::active());
    }

    void applyScheme (const CurrentTheme::Scheme& s)
    {
        // Start from JUCE's matching base scheme so anything we don't touch
        // (scrollbars, default widget states) stays legible in each theme.
        setColourScheme (s.useDarkBase ? LookAndFeel_V4::getDarkColourScheme()
                                       : LookAndFeel_V4::getLightColourScheme());

        setColour (juce::ResizableWindow::backgroundColourId, s.windowBg);

        setColour (juce::Label::textColourId,          s.text);

        setColour (juce::ComboBox::backgroundColourId, s.widgetBg);
        setColour (juce::ComboBox::textColourId,       s.text);
        setColour (juce::ComboBox::outlineColourId,    s.widgetOutline);
        setColour (juce::ComboBox::arrowColourId,      s.text);

        setColour (juce::TextButton::buttonColourId,   s.widgetBg);
        setColour (juce::TextButton::textColourOffId,  s.text);
        setColour (juce::TextButton::textColourOnId,   s.text);

        // Slider colours cover the Tempo stepper (IncDecButtons style: the
        // buttons draw from the TextButton colours above, the value box from
        // these).
        setColour (juce::Slider::textBoxTextColourId,       s.text);
        setColour (juce::Slider::textBoxBackgroundColourId, s.widgetBg);
        setColour (juce::Slider::textBoxOutlineColourId,    s.widgetOutline);

        setColour (juce::ToggleButton::textColourId,   s.text);
        setColour (juce::ToggleButton::tickColourId,   s.accent);
        setColour (juce::ToggleButton::tickDisabledColourId, s.widgetOutline);

        setColour (juce::PopupMenu::backgroundColourId,           s.panelBg);
        setColour (juce::PopupMenu::textColourId,                 s.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, s.accent);
        setColour (juce::PopupMenu::highlightedTextColourId,      s.windowBg);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (kUiFontSize));
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font (juce::FontOptions (kUiFontSize));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int /*buttonHeight*/) override
    {
        return juce::Font (juce::FontOptions (kUiFontSize));
    }
};
