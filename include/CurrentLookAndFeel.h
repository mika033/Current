#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// One LookAndFeel for the whole editor. Subclass of V4 so combos / buttons /
// labels / popup menus pick up the active theme's colours without a
// per-component setColour on every widget. applyScheme() pushes the current
// CurrentTheme scheme into the colour map; the editor calls it from its ctor and
// whenever the theme is switched.
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

        // Generic slider colours for any future slider; the menu-bar BPM
        // setter is now a StepperControl (its buttons/label draw from the
        // TextButton/Label colours above, its value box paints from the theme).
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

    // Flat-dot rotary for the module-window grid dials (octaves, gate, and the
    // like). Ported from Little Arp Monster's LamLookAndFeel: a filled body, a
    // thin outline, and an accent dot marking the value — no arc, no gradient,
    // so it reads cleanly on both themes' panels. Colours come from the active
    // scheme, so a theme swap recolours it for free.
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override
    {
        const auto& s = CurrentTheme::active();

        auto bounds  = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        auto radius  = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centreX = bounds.getCentreX();
        auto centreY = bounds.getCentreY();
        auto angle   = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour (s.widgetBg);
        g.fillEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

        g.setColour (s.widgetOutline);
        g.drawEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.5f);

        const auto dotRadius   = radius * 0.22f;
        const auto dotDistance = radius * 0.7f;
        const auto dotX = centreX + dotDistance * std::cos (angle - juce::MathConstants<float>::halfPi);
        const auto dotY = centreY + dotDistance * std::sin (angle - juce::MathConstants<float>::halfPi);

        g.setColour (s.accent);
        g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
    }
};
