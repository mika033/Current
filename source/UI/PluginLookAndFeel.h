#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// kUiFontSize per SnorkelAudioStandards design/typography.md §2: the doc
// leaves LAM's 15.0f vs WB's 14.0f as an open cross-plugin decision. Current
// picks 15.0f, matching LAM - LAM is also the canonical StepperControl
// implementation Current will pull in later, so its sizing is the more
// likely eventual cross-plugin standard.
static constexpr float kUiFontSize = 15.0f;

// LookAndFeel_V4 subclass carrying every override SnorkelAudioStandards
// mandates for combo boxes (no-arrow body, AUv3 popup parenting, popup
// anchor fix) and typography (one UI font size across all chrome). No
// rotary override yet - Phase 1 has no dials anywhere on the canvas.
class PluginLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PluginLookAndFeel();

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTabButtonFont (juce::TabBarButton&, float height) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                        int buttonX, int buttonY, int buttonW, int buttonH,
                        juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Component* getParentComponentForMenuOptions (const juce::PopupMenu::Options&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;
};
