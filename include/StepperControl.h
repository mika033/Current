#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Numeric stepper with drag-scrub + auto-repeat acceleration. Ported from
// Little Drum Machine's StepperControl, which is the shared Snorkel numeric
// setter; here it backs the Standalone menu-bar BPM setter.
//
// Button order is [ + ] [ value ] [ - ] (plus on the left) per the menu-bar
// request — the reverse of LDM's [ - ][ value ][ + ]. Only the layout differs;
// the plus button still increments and the minus button decrements.
//
// Unlike the LDM original this leans on the editor's shared CurrentLookAndFeel
// for the button/label colours instead of caching them per-component, so a
// theme switch (which repaints through that LAF) lands with no extra wiring;
// the value box is painted here directly from CurrentTheme::active(), read
// fresh every frame like the rest of the editor.
class StepperControl : public juce::Component,
                       private juce::Timer
{
public:
    StepperControl();
    ~StepperControl() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setRange (double minValue, double maxValue, double stepSize = 1.0);
    void setValue (double newValue, juce::NotificationType notification = juce::sendNotification);
    double getValue() const { return value; }

    void setFormat (const juce::String& formatString) { format = formatString; updateDisplay(); }

    std::function<void (double)> onValueChange;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updateDisplay();

    juce::TextButton incrButton;   // "+", laid out on the left
    juce::Label      valueLabel;
    juce::TextButton decrButton;   // "-", laid out on the right

    double value  = 0.0;
    double minVal = 0.0;
    double maxVal = 100.0;
    double step   = 1.0;
    juce::String format = "%.0f";

    static constexpr int kButtonW = 25;

    int repeatDirection = 0;
    int repeatIntervalMs = 0;
    static constexpr int repeatInitialDelayMs = 400;
    static constexpr int repeatMinIntervalMs  = 30;

    double dragStartValue = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepperControl)
};
