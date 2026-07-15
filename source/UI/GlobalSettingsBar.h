#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Menu bar above the canvas (requirements doc "Menu bar"). Phase 1 only
// carries the global root/scale/quantize settings; Edit (Copy/Paste/
// Duplicate) needs a selection model the requirements doc leaves open, and
// Load/Save is explicitly out of Phase 1 scope.
class GlobalSettingsBar : public juce::Component
{
public:
    GlobalSettingsBar();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label rootLabel { {}, "Root" };
    juce::ComboBox rootBox;

    juce::Label scaleLabel { {}, "Scale" };
    juce::ComboBox scaleBox;

    juce::TextButton quantizeButton { "Quantize: On" };
};
